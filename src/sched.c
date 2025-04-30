#include "sched.h"
#include "irq.h"
#include "utils.h"
#include "mm.h"
#include "debug.h"
#include "board.h"
#include "vm.h"
#include "cpu_core.h"
#include "spinlock.h"

// idle vcpu や動的に作られた vcpu などへの参照を保持する配列
// todo: 直接触らせないようにする
// todo: vCPU や VM が終了し、ID が再利用される場合は、単純に VM 数を数えるだけではダメ

// 現在実行中の vCPU の管理リストと保持数(idle_vms があるので初期値は NUMBER_OF_PCPUS)
struct vcpu_struct *vcpus[NUMBER_OF_VCPUS];
int current_number_of_vcpus = NUMBER_OF_PCPUS;

// 現在実行中の VM の管理リストと保持数(idle_vms があるので初期値は 1)
struct vm_struct2 *vms2[NUMBER_OF_VMS];
int current_number_of_vms = 1;

void set_cpu_virtual_interrupt(struct vcpu_struct *vcpu) {
	// もし current の VM に対して irq が発生していたら、仮想割込みを設定する
	if (HAVE_FUNC(vcpu->vm->board_ops, is_irq_asserted) && vcpu->vm->board_ops->is_irq_asserted(vcpu)) {
		assert_virq();
	}
	else {
		clear_virq();
	}

	// fiq も同様
	if (HAVE_FUNC(vcpu->vm->board_ops, is_fiq_asserted) && vcpu->vm->board_ops->is_fiq_asserted(vcpu)) {
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
	current_pcpu()->current_vcpu->state = VCPU_ZOMBIE;

	yield();
}

void set_cpu_sysregs(struct vcpu_struct *vcpu) {
	set_stage2_pgd(vcpu->vm->mm.first_table, vcpu->vm->vmid);
	restore_sysregs(&vcpu->cpu_sysregs);
}

// ハイパーバイザでの処理を終えて VM に処理を戻すときに kernel_exit から呼ばれる
void vm_entering_work() {
	struct vcpu_struct *vcpu = current_pcpu()->current_vcpu;

	if (HAVE_FUNC(vcpu->vm->board_ops, entering_vm)) {
		vcpu->vm->board_ops->entering_vm(vcpu);
	}

	// VM 処理に復帰するとき、コンソールがこの VM に紐づいていたら
	// キューに入っていた値を全部出力する
	if (is_uart_forwarded_vm(vcpu)) {
		flush_vm_console(vcpu);
	}

	// todo: entering_vm, flush, set_cpu_sysregs, set_cpu_virtual_interrupt の正しい呼び出し順がわからない
	// 控えておいたレジスタの値を戻す
	set_cpu_sysregs(vcpu);

	// 今実行を再開しようとしている VM に対し仮想割込みを設定する
	//   ハイパーバイザ環境では VM に対し割込みを発生させる必要があるので
	//   VM が実行開始するタイミングで仮想割込みを生成しないといけない
	set_cpu_virtual_interrupt(vcpu);
}

// VM での処理を抜けてハイパーバイザに処理に入るときに kernel_entry から呼ばれる
void vm_leaving_work() {
	struct vcpu_struct *vcpu = current_pcpu()->current_vcpu;

	// 今のレジスタの値を控える
	save_sysregs(&vcpu->cpu_sysregs);

	if (HAVE_FUNC(vcpu->vm->board_ops, leaving_vm)) {
		vcpu->vm->board_ops->leaving_vm(vcpu);
	}

	if (is_uart_forwarded_vm(vcpu)) {
		flush_vm_console(vcpu);
	}
}

const char *vm_state_str[] = {
	"RUNNING",
	"RUNNABLE",
	"ZOMBIE",
};

int find_cpu_which_runs(struct vcpu_struct *vcpu) {
	for (int i = 0; i < NUMBER_OF_PCPUS; i++) {
		if (pcpu_of(i)->current_vcpu == vcpu) {
			return i;
		}
	}
	return -1;
}

void show_vm_list() {
    printf("  %4s %3s %12s %8s %7s %9s %7s %7s %7s %7s %7s\n",
		   "vmid", "cpu", "name", "state", "pages", "saved-pc", "wfx", "hvc", "sysregs", "pf", "mmio");
    for (int i = 0; i < current_number_of_vcpus; i++) {
        struct vcpu_struct *vcpu = vcpus[i];
		int cpuid = find_cpu_which_runs(vcpu);
        printf("%c %4d   %c %12s %8s %7d %9x %7d %7d %7d %7d %7d\n",
               is_uart_forwarded_vm(vcpus[i]) ? '*' : ' ',
			   vcpu->vm->vmid,
			   // CPUID は1桁のみ対応
			   (cpuid < 0 || vcpu->state == VCPU_ZOMBIE? '-' : '0' + cpuid),
			   vcpu->vm->name ? vcpu->vm->name : "",
               vm_state_str[vcpu->state],
			   vcpu->vm->mm.vm_pages_count,
			   vcpu_pt_regs(vcpu)->pc,
               vcpu->vm->stat.wfx_trap_count,
			   vcpu->vm->stat.hvc_trap_count,
               vcpu->vm->stat.sysregs_trap_count,
			   vcpu->vm->stat.pf_trap_count,
               vcpu->vm->stat.mmio_trap_count);
    }
}

// EL2 から EL1 に遷移し、VM を復帰させる
static void schedule(struct vcpu_struct *vcpu) {
	struct pcpu_struct *pcpu = current_pcpu();

	vcpu->state = VCPU_RUNNING;
	pcpu->current_vcpu = vcpu;

	// しばらく vcpu を実行する
	cpu_switch_to(&pcpu->scheduler_context, vcpu);

	// ここに戻ってきたら、今まで動いていた VM を停止させる
	vcpu->state = (vcpu->state == VCPU_ZOMBIE) ? VCPU_ZOMBIE : VCPU_RUNNABLE;
	pcpu->current_vcpu = NULL;
}

// 各コア専用に用意された idle vcpu で実行され、タイマ割込みが発生するとここに帰ってくる
// 切り替える前に必ず VM のロックを取り、切り替え終わったらすぐにロックを解放する
// todo: 割込みを無効にしないといけないタイミングがありそう
// todo: hypervisor 実行中に割込みは処理してもいいが、コンテキストスイッチはしてはいけない
void scheduler(unsigned long cpuid) {
	struct vcpu_struct *vcpu;
	int found;

	// この CPU コアの割込みを有効化
	enable_irq();

	// todo: 割込みをどうするか考える、ただしタスクスイッチは禁止しないといけない
	while (1) {
		found = 0;

		// 単純なラウンドロビンで vCPU に CPU 時間を割り当てる
		// 先頭の vCPU は idle vCPU なので飛ばす
		for (int i = NUMBER_OF_PCPUS; i < NUMBER_OF_VCPUS; i++) {
			vcpu = vcpus[i];

			// vCPU が無効な場合はスキップ
			if (!vcpu) {
				continue;
			}

			acquire_lock(&vcpu->lock);

			// RUNNABLE 状態の vCPU を探す
			if (vcpu && vcpu->state == VCPU_RUNNABLE) {
				found = 1;
				schedule(vcpu);
			}

			release_lock(&vcpu->lock);
		}

		// 全 vCPU を走査しても実行できる vCPU がひとつも見つからなかったら IDLE vCPU を実行
		if (!found) {
			vcpu = vcpus[cpuid];
			acquire_lock(&vcpu->lock);
			schedule(vcpu);
			release_lock(&vcpu->lock);
		}
	}
}

// CPU 時間を手放し VM を切り替える
void yield() {
	struct pcpu_struct *pcpu = current_pcpu();
	struct vcpu_struct *vcpu = pcpu->current_vcpu;

	// ロックを取ってから idle_vm に切り替える
	acquire_lock(&vcpu->lock);

	// 割込みの有効・無効状態は CPU の状態ではなくこのスレッドの状態なので、退避・復帰させる必要がある
	int interrupt_enable = current_pcpu()->interrupt_enable;

	// スケジューラに復帰
	cpu_switch_to(vcpu, &pcpu->scheduler_context);

	current_pcpu()->interrupt_enable = interrupt_enable;

	// また戻ってきたらロックを解放する
	release_lock(&vcpu->lock);
}
