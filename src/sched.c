#include "sched.h"
#include "irq.h"
#include "utils.h"
#include "mm.h"
#include "debug.h"
#include "board.h"
#include "vm.h"
#include "cpu_core.h"
#include "spinlock.h"

static struct vm_struct init_vm = {
	.cpu_context = {0},
	.state       = 0,
	.counter     = 0,
	.priority    = 1,
	.vmid        = 0,
	.flags       = 0,
	.name        = "",
	.board_ops   = 0,
	.board_data  = 0,
	.mm          = {0},
	.cpu_sysregs = {0},
	.stat        = {0},
	.console     = {0},
	.lock        = {0, 0, -1},
};

static struct vm_struct idle_vms[NUMBER_OF_CPU_CORES];
// idle vm や動的に作られた vm などへの参照を保持する配列
// todo: 直接触らせないようにする
struct vm_struct *vms[NUMBER_OF_VMS];

// 各 CPU コアで実行中の VM
// todo: CPU 構造体に入れたい
struct vm_struct *currents[NUMBER_OF_CPU_CORES];

// 現在実行中の VM の数(idle_vms があるので初期値は NUMBER_OF_CPU_CORES)
int current_number_of_vms = NUMBER_OF_CPU_CORES;

void initiate_idle_vms() {
	for (int i = 0; i < NUMBER_OF_CPU_CORES; i++) {
		currents[i] = &idle_vms[i];
		memcpy(&idle_vms[i], &init_vm, sizeof(struct vm_struct));

		vms[i] = &idle_vms[i];
		vms[i]->name = "IDLE";
		vms[i]->vmid = i;
		vms[i]->state = VM_ZOMBIE;

		// IDLE VM 用の ホスト用のコンソールの初期化
		init_vm_console(vms[i]);
	}
}

// 現在実行中の VM の vm_struct
struct vm_struct *current_vm() {
	return currents[get_cpuid()];
}

void set_current_vm(struct vm_struct *vm) {
	currents[get_cpuid()] = vm;
}

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
	current_vm()->state = VM_ZOMBIE;

	// todo: exit_vm したあとは割込みが無効のままになり、正しく復帰できない
	yield();
}

void set_cpu_sysregs(struct vm_struct *tsk) {
	set_stage2_pgd(tsk->mm.first_table, tsk->vmid);
	restore_sysregs(&tsk->cpu_sysregs);
}

// ハイパーバイザでの処理を終えて VM に処理を戻すときに kernel_exit から呼ばれる
void vm_entering_work() {
	struct vm_struct *vm = current_vm();

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
	struct vm_struct *vm = current_vm();

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

// todo: cpu 構造体に vm 情報を入れたら不要になるはず
int find_cpu_which_runs(struct vm_struct *vm) {
	for (int i = 0; i < NUMBER_OF_CPU_CORES; i++) {
		if (currents[i] == vm) {
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

// 各コア専用に用意された idle vm で実行され、タイマ割込みが発生するとここに帰ってくる
// 切り替える前に必ず VM のロックを取り、切り替え終わったらすぐにロックを解放する
// todo: 割込みを無効にしないといけないタイミングがありそう
// todo: vm の数が減って idle のままになる cpu コアが出ると割込みがマスクされてしまう
//       タイマ割込みが発生したあとはずっと割込み無効になっている
//  -> 今の実装だと idle_vm がスケジューラそのものになっている
//     cpu_switch_to から戻ってきたあと実行する vm が見つからないと、どの vm も実行されていない状態になる
//     つまり release_lock されないので割込みが禁止されたままになる
// todo: wfi ループする idle_vm を作って、優先度最低でそこに遷移させるようにする
//
// idle vm は必要、hypervisor 自体のコンテキストも必要
// 今は idle vm が el2 hypervisor と一緒になってしまっている
// hypervisor 実行中に割込みは処理してもいいが、コンテキストスイッチはしてはいけない
void scheduler(unsigned long cpuid) {
	struct vm_struct *vm;

	// todo: 割込みをどうするか考える、ただしタスクスイッチは禁止しないといけない
	while (1) {
		// 単純なラウンドロビンで VM に CPU 時間を割り当てる
		// 先頭の VM は idle vm なので飛ばす
		// todo: idle_vm は vms に入れなくてもいいのではないか？
		for (int i = NUMBER_OF_CPU_CORES; i < NUMBER_OF_VMS; i++) {
			vm = vms[i];

			acquire_lock(&vm->lock);

			// RUNNABLE 状態の VM を探す
			if (vm && vm->state == VM_RUNNABLE) {
				// 準備をして、この VM を復帰させる
				vm->state = VM_RUNNING;
				idle_vms[cpuid].state = VM_RUNNABLE;
				current_cpu_core()->current_vm = vm;
				set_current_vm(vm);

				// しばらく vm を実行する
				cpu_switch_to(&idle_vms[cpuid], vm);

				// ここに戻ってきたら、今まで動いていた VM を停止させる
				vm->state = vm->state == VM_ZOMBIE ? VM_ZOMBIE : VM_RUNNABLE;
				idle_vms[cpuid].state = VM_RUNNING;
				current_cpu_core()->current_vm = &idle_vms[cpuid];
				set_current_vm(&idle_vms[cpuid]);
			}

			release_lock(&vm->lock);
		}
		// todo: これは？今の実装でも関係あるか？
		// 割込みを有効にしておかないと誰も VM の状態を変更できず無限ループになってしまうので
		// timer_tick で割込みを有効にしておく必要がある
		// ただし、割込みは許可されるが preemption(タスク(VM)切り替え)は許可されていないことに注意
	}
}

// CPU 時間を手放し VM を切り替える
void yield() {
	struct vm_struct *vm = current_vm();

	// ロックを取ってから idle_vm に切り替える
	acquire_lock(&vm->lock);

	// 割込みの有効・無効状態は CPU の状態ではなくこのスレッドの状態なので、退避・復帰させる必要がある
	int interrupt_enable = current_cpu_core()->interrupt_enable;
	cpu_switch_to(vm, &idle_vms[get_cpuid()]);
	current_cpu_core()->interrupt_enable = interrupt_enable;

	// また戻ってきたらロックを解放する
	release_lock(&vm->lock);
}
