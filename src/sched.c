#include "sched.h"
#include "irq.h"
#include "utils.h"
#include "mm.h"
#include "debug.h"
#include "board.h"

static struct task_struct init_task = INIT_TASK;
// 現在実行中のタスクの task_struct
struct task_struct *current = &(init_task);
struct task_struct *task[NR_TASKS] = {&(init_task), };
// 現在実行中のタスクの数(init_task があるので初期値は1)
int nr_tasks = 1;

// タイマ割込みが発生してもタスク切り替えを行わないようにする
void preempt_disable(void)
{
	current->preempt_count++;
}

// タスク切り替えを許可する
void preempt_enable(void)
{
	current->preempt_count--;
}


void _schedule(void)
{
	// タスク切り替え中はタスク切り替えが発生しないようにする
	preempt_disable();
	int next,c;
	struct task_struct * p;
	while (1) {
		c = -1;
		next = 0;
		// タスクの数は決め打ちで NR_TASKS 個
		// 先頭から順番に状態を見ていく
		for (int i = 0; i < NR_TASKS; i++){
			p = task[i];
			// RUNNING 状態で、かつ一番カウンタが大きいものを探す
			if (p && p->state == TASK_RUNNING && p->counter > c) {
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
	// 再びタスク切り替えを有効にして戻る
	preempt_enable();
}

// プロセスを切り替える
void schedule(void)
{
	// 自主的に CPU を手放した場合はカウンタを 0 にする
	current->counter = 0;
	_schedule();
}

void set_cpu_sysregs(struct task_struct *task) {
	// アドレス空間を切り替え
	set_stage2_pgd(task->mm.first_table, task->pid);
	_set_sysregs(&(task->cpu_sysregs));
}

void set_cpu_virtual_interrupt(struct task_struct *task) {
	// もし current のタスク(VM)に対して irq が発生していたら、仮想割込みを設定する
	if (HAVE_FUNC(task->board_ops, is_irq_asserted) && task->board_ops->is_irq_asserted(current)) {
		assert_virq();
	}
	else {
		clear_virq();
	}

	// fiq も同様
	if (HAVE_FUNC(task->board_ops, is_fiq_asserted) && task->board_ops->is_fiq_asserted(current)) {
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

	// 既に current は next(切り替え先のタスク)になっている
	set_cpu_sysregs(current);

	// レジスタを控えて実際にタスクを切り替える
	// 戻ってくるときは別のタスクになっている
	cpu_switch_to(prev, next);
}

void schedule_tail(void) {
	preempt_enable();
}

// タイマが発火すると呼ばれ、タスク切り替えを行う
void timer_tick()
{
	--current->counter;
	// まだタスクが十分な時間実行されていなかったり(counter > 0)
	// タスク切り替えが禁止されていたら(preempt_count > 0)
	// 切り替えずに終了
	if (current->counter > 0 || current->preempt_count > 0) {
		return;
	}
	current->counter = 0;
	// 割込みハンドラは割込み無効状態で開始される
	// _schedule 関数の処理中に割込みを使う部分があるので割込みを有効にする
	// todo: なぜ無効のままでよくなった？ EL2 だと割込み発生しない？
	//enable_irq();
	_schedule();
	//disable_irq();
}

void exit_task(){
	preempt_disable();
	for (int i = 0; i < NR_TASKS; i++){
		if (task[i] == current) {
			// 実行中のプロセスの構造体を見つけて zombie にする(=スケジューリング対象から外れる)
			// todo: メモリは解放しなくていい？ (free_page を呼ぶ関数がない…)
			task[i]->state = TASK_ZOMBIE;
			break;
		}
	}
	preempt_enable();
	schedule();
}

// ハイパーバイザでの処理を終えて VM に処理を戻すときに呼ばれる
void vm_entering_work() {
	// 今実行を再開しようとしているタスク(VM)に対し仮想割込みを設定する
	//   ハイパーバイザ環境では VM に対し割込みを発生させる必要があるので
	//   VM が実行開始するタイミングで仮想割込みを生成しないといけない
	set_cpu_virtual_interrupt(current);

	if (HAVE_FUNC(current->board_ops, entering_vm)) {
		current->board_ops->entering_vm(current);
	}
}

// VM での処理を抜けてハイパーバイザに処理に入るときに呼ばれる
void vm_leaving_work() {
	if (HAVE_FUNC(current->board_ops, leaving_vm)) {
		current->board_ops->leaving_vm(current);
	}
}

const char *task_state_str[] = {
	"RUNNING",
	"ZOMBIE",
};

void show_task_list() {
    preempt_disable();
	printf("pid state     pages   traps\n");
    for (int i = 0; i < nr_tasks; i++) {
        struct task_struct *tsk = task[i];
        printf("%3d %8s %7d %7d\n", tsk->pid, task_state_str[tsk->state],
               tsk->mm.user_pages_count, tsk->stat.trap_count);
    }
    preempt_enable();
}
