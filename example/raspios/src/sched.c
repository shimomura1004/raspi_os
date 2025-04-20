#include "sched.h"
#include "irq.h"
#include "printf.h"
#include "utils.h"
#include "mm.h"
#include "spinlock.h"

static struct task_struct init_task = INIT_TASK;
struct task_struct *current = &(init_task);
struct task_struct * task[NR_TASKS] = {&(init_task), };
int nr_tasks = 1;

struct spinlock lock = {0, "", -1};

void preempt_disable(void)
{
	current->preempt_count++;
}

void preempt_enable(void)
{
	current->preempt_count--;
}


void _schedule(void)
{
	preempt_disable();
	int next,c;
	struct task_struct * p;

	// ここではデータを操作しない、切り替え先を探すだけ
	while (1) {
		c = -1;
		next = 0;
		for (int i = 0; i < NR_TASKS; i++){
			p = task[i];
			if (p && p->state != TASK_ZOMBIE && p->counter > c) {
				c = p->counter;
				next = i;
			}
		}
		if (c) {
			break;
		}
		for (int i = 0; i < NR_TASKS; i++) {
			p = task[i];
			if (p) {
				p->counter = (p->counter >> 1) + p->priority;
			}
		}
	}

	// 実際に切り替える
	// switch_to を呼ぶと、切り替えに成功した場合は呼び出したプロセスのコンテキストには戻ってこず
	// 切り替え先のプロセスのコンテキストでここに戻ってくる
	switch_to(task[next]);

	preempt_enable();
}

void schedule(void)
{
	current->counter = 0;
	_schedule();
}

// コンテキスト切り替えが不要だった場合は -1
// 実際にコンテキストが切り替わった場合は 0
// switch_to を呼び出したプロセスにすぐ値が戻るわけではなく、
// しばらくしてまたこのプロセスに CPU 時間が回ってきたあとに返される値であることに注意
// 切り替わった先のプロセスがこの関数が返す値は、自分が前回 switch_to を呼び出したときの結果
int switch_to(struct task_struct * next) 
{
printf("switch from 0x%x to 0x%x\n", current, next);
	// 切り替えが不要だった場合はなにもせず -1 を返す
	if (current == next || next->state == TASK_RUNNING) {
printf("no switch\n");
		return -1;
	}

	struct task_struct * prev = current;
	current = next;
	set_pgd(next->mm.pgd);

	prev->state = TASK_RUNNABLE;
	cpu_switch_to(prev, next);
	prev->state = TASK_RUNNING;
printf("now 0x%x\n", current);

	// cpu_switch_to で別のプロセスに切り替わったあと、
	// しばらくしてまたこのプロセスのコンテキストに戻ってきた場合は、
	// 切り替えが成功していたとして 0 を返す
	return 0;
}

void schedule_tail(void) {
	preempt_enable();
}


void timer_tick()
{
	--current->counter;
	if (current->counter>0 || current->preempt_count >0) {
		return;
	}
	current->counter=0;
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
