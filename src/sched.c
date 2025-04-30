#include "sched.h"
#include "irq.h"
#include "utils.h"
#include "mm.h"
#include "debug.h"
#include "board.h"
#include "vm.h"
#include "cpu_core.h"
#include "spinlock.h"

// idle vm や動的に作られた vm などへの参照を保持する配列
// todo: 直接触らせないようにする
struct vm_struct *vms[NUMBER_OF_VMS];

// 現在実行中の VM の数(idle_vms があるので初期値は NUMBER_OF_CPU_CORES)
int current_number_of_vms = NUMBER_OF_CPU_CORES;

void set_cpu_virtual_interrupt(struct vm_struct *tsk) {
	// もし current の VM に対して irq が発生していたら、仮想割込みを設定する
	if (HAVE_FUNC(tsk->board_ops, is_irq_asserted) && tsk->board_ops->is_irq_asserted(tsk)) {
		assert_virq();
	}
	else {
		clear_virq();
	}

	// fiq も同様
	if (HAVE_FUNC(tsk->board_ops, is_fiq_asserted) && tsk->board_ops->is_fiq_asserted(tsk)) {
		assert_vfiq();
	}
	else {
		clear_vfiq();
	}

	// todo: vserror は？
}

// タイマが発火すると呼ばれ、VM 切り替えを行う
void timer_tick() {
	yield();
}

void exit_vm(){
	// todo: リソースを解放する(コンソールとかメモリページとか)

	// 実行中の VM のstate を zombie にする(=スケジューリング対象から外れる)
	current_cpu_core()->current_vm->state = VM_ZOMBIE;

	yield();
}

void set_cpu_sysregs(struct vm_struct *tsk) {
	set_stage2_pgd(tsk->mm.first_table, tsk->vmid);
	restore_sysregs(&tsk->cpu_sysregs);
}

// ハイパーバイザでの処理を終えて VM に処理を戻すときに kernel_exit から呼ばれる
void vm_entering_work() {
	struct vm_struct *vm = current_cpu_core()->current_vm;

	if (HAVE_FUNC(vm->board_ops, entering_vm)) {
		vm->board_ops->entering_vm(vm);
	}

	// VM 処理に復帰するとき、コンソールがこの VM に紐づいていたら
	// キューに入っていた値を全部出力する
	if (is_uart_forwarded_vm(vm)) {
		flush_vm_console(vm);
	}

	// todo: entering_vm, flush, set_cpu_sysregs, set_cpu_virtual_interrupt の正しい呼び出し順がわからない
	// 控えておいたレジスタの値を戻す
	set_cpu_sysregs(vm);

	// 今実行を再開しようとしている VM に対し仮想割込みを設定する
	//   ハイパーバイザ環境では VM に対し割込みを発生させる必要があるので
	//   VM が実行開始するタイミングで仮想割込みを生成しないといけない
	set_cpu_virtual_interrupt(vm);
}

// VM での処理を抜けてハイパーバイザに処理に入るときに kernel_entry から呼ばれる
void vm_leaving_work() {
	struct vm_struct *vm = current_cpu_core()->current_vm;

	// 今のレジスタの値を控える
	save_sysregs(&vm->cpu_sysregs);

	if (HAVE_FUNC(vm->board_ops, leaving_vm)) {
		vm->board_ops->leaving_vm(vm);
	}

	if (is_uart_forwarded_vm(vm)) {
		flush_vm_console(vm);
	}
}

const char *vm_state_str[] = {
	"RUNNING",
	"RUNNABLE",
	"ZOMBIE",
};

int find_cpu_which_runs(struct vm_struct *vm) {
	for (int i = 0; i < NUMBER_OF_CPU_CORES; i++) {
		if (cpu_core(i)->current_vm == vm) {
			return i;
		}
	}
	return -1;
}

void show_vm_list() {
    printf("  %4s %3s %12s %8s %7s %9s %7s %7s %7s %7s %7s\n",
		   "vmid", "cpu", "name", "state", "pages", "saved-pc", "wfx", "hvc", "sysregs", "pf", "mmio");
    for (int i = 0; i < current_number_of_vms; i++) {
        struct vm_struct *vm = vms[i];
		int cpuid = find_cpu_which_runs(vm);
        printf("%c %4d   %c %12s %8s %7d %9x %7d %7d %7d %7d %7d\n",
               is_uart_forwarded_vm(vms[i]) ? '*' : ' ',
			   vm->vmid,
			   // CPUID は1桁のみ対応
			   (cpuid < 0 || vm->state == VM_ZOMBIE? '-' : '0' + cpuid),
			   vm->name ? vm->name : "",
               vm_state_str[vm->state],
			   vm->mm.vm_pages_count,
			   vm_pt_regs(vm)->pc,
               vm->stat.wfx_trap_count,
			   vm->stat.hvc_trap_count,
               vm->stat.sysregs_trap_count,
			   vm->stat.pf_trap_count,
               vm->stat.mmio_trap_count);
    }
}

// EL2 から EL1 に遷移し、VM を復帰させる
static void schedule(struct vm_struct *vm) {
	struct cpu_core_struct *cpu_core = current_cpu_core();

	vm->state = VM_RUNNING;
	cpu_core->current_vm = vm;

	// しばらく vm を実行する
	cpu_switch_to(&cpu_core->scheduler_context, vm);

	// ここに戻ってきたら、今まで動いていた VM を停止させる
	vm->state = vm->state == VM_ZOMBIE ? VM_ZOMBIE : VM_RUNNABLE;
	cpu_core->current_vm = NULL;
}

// 各コア専用に用意された idle vm で実行され、タイマ割込みが発生するとここに帰ってくる
// 切り替える前に必ず VM のロックを取り、切り替え終わったらすぐにロックを解放する
// todo: 割込みを無効にしないといけないタイミングがありそう
// todo: hypervisor 実行中に割込みは処理してもいいが、コンテキストスイッチはしてはいけない
void scheduler(unsigned long cpuid) {
	struct vm_struct *vm;
	int found;

	// この CPU コアの割込みを有効化
	enable_irq();

	// todo: 割込みをどうするか考える、ただしタスクスイッチは禁止しないといけない
	while (1) {
		found = 0;

		// 単純なラウンドロビンで VM に CPU 時間を割り当てる
		// 先頭の VM は idle vm なので飛ばす
		for (int i = NUMBER_OF_CPU_CORES; i < NUMBER_OF_VMS; i++) {
			vm = vms[i];

			// そもそも VM がない場合はスキップ
			if (!vm) {
				continue;
			}

			acquire_lock(&vm->lock);

			// RUNNABLE 状態の VM を探す
			if (vm && vm->state == VM_RUNNABLE) {
				found = 1;
				schedule(vm);
			}

			release_lock(&vm->lock);
		}

		// 全 VM を走査しても実行できる VM がひとつも見つからなかったら IDLE VM を実行
		if (!found) {
			vm = vms[cpuid];
			acquire_lock(&vm->lock);
			schedule(vm);
			release_lock(&vm->lock);
		}
	}
}

// CPU 時間を手放し VM を切り替える
void yield() {
	struct cpu_core_struct *cpu_core = current_cpu_core();
	struct vm_struct *vm = cpu_core->current_vm;

	// ロックを取ってから idle_vm に切り替える
	acquire_lock(&vm->lock);

	// 割込みの有効・無効状態は CPU の状態ではなくこのスレッドの状態なので、退避・復帰させる必要がある
	int interrupt_enable = current_cpu_core()->interrupt_enable;

	// スケジューラに復帰
	cpu_switch_to(vm, &cpu_core->scheduler_context);

	current_cpu_core()->interrupt_enable = interrupt_enable;

	// また戻ってきたらロックを解放する
	release_lock(&vm->lock);
}
