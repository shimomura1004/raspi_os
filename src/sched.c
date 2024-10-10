#include "sched.h"
#include "irq.h"
#include "printf.h"
#include "utils.h"
#include "mm.h"

static struct task_struct init_task = INIT_TASK;
struct task_struct *current = &(init_task);
struct task_struct * task[NR_TASKS] = {&(init_task), };
// タスクの数
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
		// カウンタが 0 以外のものを見つけていたらループを抜ける
		if (c) {
			break;
		}
		// 探しなおす前に各タスクのカウンタを更新
		for (int i = 0; i < NR_TASKS; i++) {
			p = task[i];
			if (p) {
				// 今のカウンタの値を半分にして、プライオリティを足したもので更新
				p->counter = (p->counter >> 1) + p->priority;
			}
		}
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


// 指定したタスクに切り替える
void switch_to(struct task_struct * next) 
{
	if (current == next) 
		return;
	struct task_struct * prev = current;
	current = next;
	// アドレス空間を切り替え
	set_pgd(next->mm.pgd);
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
	// todo: 条件がよくわからないが、タイマが発火してもタスク切り替えしないこともある
	if (current->counter>0 || current->preempt_count >0) {
		return;
	}
	current->counter=0;
	// 割込みハンドラは割込み無効状態で開始される
	// _schedule 関数の処理中に割込みを使う部分があるので割込みを有効にする
	enable_irq();
	_schedule();
	disable_irq();
}

void exit_process(){
	preempt_disable();
	for (int i = 0; i < NR_TASKS; i++){
		if (task[i] == current) {
			task[i]->state = TASK_ZOMBIE;
			break;
		}
	}
	preempt_enable();
	schedule();
}
