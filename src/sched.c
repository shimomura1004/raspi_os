#include "sched.h"
#include "irq.h"
#include "utils.h"
#include "mm.h"
#include "debug.h"
#include "board.h"
#include "vm.h"

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

// 現在実行中の VM の vm_struct
// todo: cpu コアが複数あるのに current が 1 つしかない！
//       削除して、cpu_core_struct の current_vm に置き換える
struct vm_struct *current = &(init_vm);
struct vm_struct *vms[NUMBER_OF_VMS] = {&(init_vm), };
// 現在実行中の VM の数(init_vm があるので初期値は1)
int current_number_of_vms = 1;

//  VM 切換え
// 複数の CPU が同時に呼び出すのでスレッドセーフにしないといけない
// todo: 複数 CPU で動かす場合は、停止時と異なる CPU で VM が動くかもしれない
static void _schedule(void)
{
	int next, c;
	struct vm_struct *p;

	while (1) {
		c = -1;
		next = 0;
		// VM の最大数は決め打ちで NUMBER_OF_VMS 個
		// 先頭から順番に状態を見ていく
		for (int i = 0; i < NUMBER_OF_VMS; i++){
			p = vms[i];
			acquire_lock(&p->lock);

			// RUNNING/RUNNABLE 状態で、かつ一番カウンタが大きいものを探す
			if (p && p->state != VM_ZOMBIE && p->counter > c) {
				c = p->counter;
				next = i;
			}

			release_lock(&p->lock);
		}
		// まだ実行時間(counter)が残っているものがあったらそれを実行する
		if (c) {
			break;
		}

		// すべての VM が実行時間を使い切っていたら、全 VM に実行時間を補充する
		// todo: おそらくロックが必要
		for (int i = 0; i < NUMBER_OF_VMS; i++) {
			p = vms[i];
			if (p) {
				// 何回もループした場合にカウンタの値が大きくなりすぎないように
				// 今のカウンタの値を半分にして、プライオリティを足したもので更新
				p->counter = (p->counter >> 1) + p->priority;
			}
		}
		// VM_RUNNING 状態のものが見つかるまでずっとループする
		// 割込みを有効にしておかないと誰も VM の状態を変更できず無限ループになってしまうので
		// timer_tick で割込みを有効にしておく必要がある
		// ただし、割込みは許可されるが preemption(タスク(VM)切り替え)は許可されていないことに注意
	}

	// 切り替え先 VM に switch_to する
	switch_to(vms[next]);
}

// 自主的に CPU 時間を手放し VM を切り替える
// todo: schedule を呼ぶ前に手動で IRQ を無効化する必要があり危険
//       現状 schedule を呼ぶのは、main, handle_trap_wfx, exit_vm のみ
//       main は手動で無効化している
//       handle_trap_wfx は割込から呼ばれるので、割込みは無効化されている
//       exit_vm は PANIC マクロから呼ばれる、割込みが無効かどうかわからず危ないのでは
void schedule(void)
{
	// 自主的に CPU を手放した場合はカウンタを 0 にする
	current->counter = 0;
	_schedule();
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

// 指定した VM に切り替える
void switch_to(struct vm_struct * next)
{
	if (current == next) {
		return;
	}

	struct vm_struct * prev = current;
	current = next;

	// レジスタを控えて実際に VM を切り替える
	// 戻ってくるときは別の VM になっている
	cpu_switch_to(prev, next);
}

// タイマが発火すると呼ばれ、VM 切り替えを行う
void timer_tick()
{
	--current->counter;
	// まだ VM が十分な時間実行されていなかったら切り替えずに終了
	if (current->counter > 0) {
		return;
	}
	current->counter = 0;

	// 割込みハンドラは割込み無効状態で開始される
	// _schedule 関数の処理中に割込みを使う部分があるので割込みを有効にする
	// todo: なぜ無効のままでよくなった？
	//enable_irq();
	_schedule();
	//disable_irq();
}

void exit_vm(){
	for (int i = 0; i < NUMBER_OF_VMS; i++){
		if (vms[i] == current) {
			// 実行中の VM の構造体を見つけて zombie にする(=スケジューリング対象から外れる)
			// todo: メモリは解放しなくていい？
			vms[i]->state = VM_ZOMBIE;
			break;
		}
	}

	schedule();
}

void set_cpu_sysregs(struct vm_struct *tsk) {
	set_stage2_pgd(tsk->mm.first_table, tsk->vmid);
	restore_sysregs(&tsk->cpu_sysregs);
}

// ハイパーバイザでの処理を終えて VM に処理を戻すときに kernel_exit から呼ばれる
void vm_entering_work() {
	if (HAVE_FUNC(current->board_ops, entering_vm)) {
		current->board_ops->entering_vm(current);
	}

	// VM 処理に復帰するとき、コンソールがこの VM に紐づいていたら
	// キューに入っていた値を全部出力する
	if (is_uart_forwarded_vm(current)) {
		flush_vm_console(current);
	}

	// todo: entering_vm, flush, set_cpu_sysregs, set_cpu_virtual_interrupt の正しい呼び出し順がわからない
	// 控えておいたレジスタの値を戻す
	set_cpu_sysregs(current);

	// 今実行を再開しようとしている VM に対し仮想割込みを設定する
	//   ハイパーバイザ環境では VM に対し割込みを発生させる必要があるので
	//   VM が実行開始するタイミングで仮想割込みを生成しないといけない
	set_cpu_virtual_interrupt(current);
}

// VM での処理を抜けてハイパーバイザに処理に入るときに kernel_entry から呼ばれる
void vm_leaving_work() {
	// 今のレジスタの値を控える
	save_sysregs(&current->cpu_sysregs);

	if (HAVE_FUNC(current->board_ops, leaving_vm)) {
		current->board_ops->leaving_vm(current);
	}

	if (is_uart_forwarded_vm(current)) {
		flush_vm_console(current);
	}
}

const char *vm_state_str[] = {
	"RUNNING",
	"RUNNABLE",
	"ZOMBIE",
};

// todo: VM に割り当てられた CPU ID も表示したい
void show_vm_list() {
    printf("  %4s %12s %8s %7s %9s %7s %7s %7s %7s %7s\n",
		   "vmid", "name", "state", "pages", "saved-pc", "wfx", "hvc", "sysregs", "pf", "mmio");
    for (int i = 0; i < current_number_of_vms; i++) {
        struct vm_struct *vm = vms[i];
        printf("%c %4d %12s %8s %7d %9x %7d %7d %7d %7d %7d\n",
               is_uart_forwarded_vm(vms[i]) ? '*' : ' ',
			   vm->vmid,
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
