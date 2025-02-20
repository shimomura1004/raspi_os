#include "sched.h"
#include "irq.h"
#include "utils.h"
#include "mm.h"
#include "debug.h"
#include "board.h"
#include "task.h"

static struct task_struct init_task = INIT_TASK;
// 現在実行中のタスクの task_struct
struct task_struct *current = &(init_task);
struct task_struct *task[NR_TASKS] = {&(init_task), };
// 現在実行中のタスクの数(init_task があるので初期値は1)
int nr_tasks = 1;

// タスク切換え
// 複数の CPU が同時に呼び出すのでスレッドセーフにしないといけない
// todo: 複数 CPU で動かす場合は、停止時と異なる CPU で VM が動くかもしれない
static void _schedule(void)
{
	int next, c;
	struct task_struct *p;

	while (1) {
		c = -1;
		next = 0;
		// タスクの数は決め打ちで NR_TASKS 個
		// 先頭から順番に状態を見ていく
		for (int i = 0; i < NR_TASKS; i++){
			p = task[i];
			// RUNNING/RUNNABLE 状態で、かつ一番カウンタが大きいものを探す
			if (p && p->state != TASK_ZOMBIE && p->counter > c) {
				c = p->counter;
				next = i;
			}
		}
		// まだ実行時間(counter)が残っているものがあったらそれを実行する
		if (c) {
			break;
		}
		// すべてのタスクが実行時間を使い切っていたら、全タスクに実行時間を補充する
		for (int i = 0; i < NR_TASKS; i++) {
			p = task[i];
			if (p) {
				// 何回もループした場合にカウンタの値が大きくなりすぎないように
				// 今のカウンタの値を半分にして、プライオリティを足したもので更新
				p->counter = (p->counter >> 1) + p->priority;
			}
		}
		// TASK_RUNNING 状態のものが見つかるまでずっとループする
		// 割込みを有効にしておかないと誰もタスクの状態を変更できず無限ループになってしまうので
		// timer_tick で割込みを有効にしておく必要がある
		// ただし、割込みは許可されるが preemption(タスク切り替え)は許可されていないことに注意
	}

	// 切り替え先タスクを見つけて switch_to する
	switch_to(task[next]);
}

// 自主的に CPU 時間を手放しプロセスを切り替える
// todo: schedule を呼ぶ前に手動で IRQ を無効化する必要があり危険
//       現状 schedule を呼ぶのは、main, handle_trap_wfx, exit_task のみ
//       main は手動で無効化している
//       handle_trap_wfx は割込から呼ばれるので、割込みは無効化されている
//       exit_task は PANIC マクロから呼ばれる、割込みが無効かどうかわからず危ないのでは
void schedule(void)
{
	// 自主的に CPU を手放した場合はカウンタを 0 にする
	current->counter = 0;
	_schedule();
}

void set_cpu_virtual_interrupt(struct task_struct *tsk) {
	// もし current のタスク(VM)に対して irq が発生していたら、仮想割込みを設定する
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

// 指定したタスクに切り替える
void switch_to(struct task_struct * next)
{
	if (current == next) {
		return;
	}

	struct task_struct * prev = current;
	current = next;

	// レジスタを控えて実際にタスクを切り替える
	// 戻ってくるときは別のタスクになっている
	cpu_switch_to(prev, next);
}

// タイマが発火すると呼ばれ、タスク切り替えを行う
void timer_tick()
{
	--current->counter;
	// まだタスクが十分な時間実行されていなかったら切り替えずに終了
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

void exit_task(){
	for (int i = 0; i < NR_TASKS; i++){
		if (task[i] == current) {
			// 実行中のプロセスの構造体を見つけて zombie にする(=スケジューリング対象から外れる)
			// todo: メモリは解放しなくていい？
			task[i]->state = TASK_ZOMBIE;
			break;
		}
	}

	schedule();
}

void set_cpu_sysregs(struct task_struct *tsk) {
	set_stage2_pgd(tsk->mm.first_table, tsk->pid);
	restore_sysregs(&tsk->cpu_sysregs);
}

// ハイパーバイザでの処理を終えて VM に処理を戻すときに kernel_exit から呼ばれる
void vm_entering_work() {
	if (HAVE_FUNC(current->board_ops, entering_vm)) {
		current->board_ops->entering_vm(current);
	}

	// VM 処理に復帰するとき、コンソールがこの VM に紐づいていたら
	// キューに入っていた値を全部出力する
	if (is_uart_forwarded_task(current)) {
		flush_task_console(current);
	}

	// todo: entering_vm, flush, set_cpu_sysregs, set_cpu_virtual_interrupt の正しい呼び出し順がわからない
	// 控えておいたレジスタの値を戻す
	set_cpu_sysregs(current);

	// 今実行を再開しようとしているタスク(VM)に対し仮想割込みを設定する
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

	if (is_uart_forwarded_task(current)) {
		flush_task_console(current);
	}
}

const char *task_state_str[] = {
	"RUNNING",
	"RUNNABLE",
	"ZOMBIE",
};

void show_task_list() {
    printf("  %3s %12s %8s %7s %9s %7s %7s %7s %7s %7s\n",
		   "pid", "name", "state", "pages", "saved-pc", "wfx", "hvc", "sysregs", "pf", "mmio");
    for (int i = 0; i < nr_tasks; i++) {
        struct task_struct *tsk = task[i];
        printf("%c %3d %12s %8s %7d %9x %7d %7d %7d %7d %7d\n",
               is_uart_forwarded_task(task[i]) ? '*' : ' ',
			   tsk->pid,
			   tsk->name ? tsk->name : "",
               task_state_str[tsk->state],
			   tsk->mm.user_pages_count,
			   task_pt_regs(tsk)->pc,
               tsk->stat.wfx_trap_count,
			   tsk->stat.hvc_trap_count,
               tsk->stat.sysregs_trap_count,
			   tsk->stat.pf_trap_count,
               tsk->stat.mmio_trap_count);
    }
}
