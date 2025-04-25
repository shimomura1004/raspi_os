#include "sched.h"
#include "irq.h"
#include "printf.h"
#include "utils.h"
#include "mm.h"
#include "spinlock.h"

static struct task_struct init_task0 = INIT_TASK;
static struct task_struct init_task1 = INIT_TASK;
static struct task_struct init_task2 = INIT_TASK;
static struct task_struct init_task3 = INIT_TASK;

struct task_struct *currents[NR_CPUS] = {&(init_task0), &(init_task1), &(init_task2), &(init_task3)};
struct task_struct *task[NR_TASKS] = {&(init_task0), &(init_task1), &(init_task2), &(init_task3)};
int nr_tasks = NR_CPUS;

struct spinlock sched_lock = {0, "sched lock", -1};

// fork で新しくプロセスを作ったあと、sched で acquire したロックを解放するための関数
// 切り替わった直後に必ずロックを解放しないといけない
void release_sched_lock() {
	release_lock(&sched_lock);
}

void preempt_disable(void)
{
	currents[get_cpuid()]->preempt_count++;
}

void preempt_enable(void)
{
	currents[get_cpuid()]->preempt_count--;
}


void _schedule(void)
{
	preempt_disable();
	int next,c;
	struct task_struct * p;

	int cpuid = get_cpuid();

	while (1) {
		// ここではデータを操作しない、切り替え先を探すだけ
		while (1) {
			c = -1;
			next = 0;
			for (int i = 0; i < NR_TASKS; i++){
				if (i < NR_CPUS && cpuid != i) {
					continue;
				}

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
				if (i < NR_CPUS && cpuid != i) {
					continue;
				}

				p = task[i];
				if (p) {
					p->counter = (p->counter >> 1) + p->priority;
				}
			}
		}

		// 実際に切り替える
		// switch_to を呼ぶと、切り替えに成功した場合は呼び出したプロセスのコンテキストには戻ってこず
		// 切り替え先のプロセスのコンテキストでここに戻ってくる
		acquire_lock(&sched_lock);
		int switched = switch_to(task[next]);
		release_lock(&sched_lock);

		if (switched) {
			break;
		}

		task[next]->counter = 0;
	}

	preempt_enable();
}

void schedule(void)
{
	currents[get_cpuid()]->counter = 0;
	_schedule();
}

// コンテキスト切り替えが不要だった場合は 0
// 実際にコンテキストが切り替わった場合は 1
// switch_to を呼び出したプロセスにすぐ値が戻るわけではなく、
// しばらくしてまたこのプロセスに CPU 時間が回ってきたあとに返される値であることに注意
// 切り替わった先のプロセスがこの関数が返す値は、自分が前回 switch_to を呼び出したときの結果
int switch_to(struct task_struct * next) 
{
	int cpuid = get_cpuid();
printf("%d: switch from 0x%x to 0x%x\n", cpuid, currents[cpuid], next);

	// 切り替えが不要だった場合はなにもせず -1 を返す
	if (currents[cpuid] == next) {
		printf("same process\n");
		return 0;
	}
	else if (next->state == TASK_RUNNING) {
		printf("already running\n");
		return 0;
	}

	struct task_struct * prev = currents[cpuid];
	currents[cpuid] = next;
	set_pgd(next->mm.pgd);

	// この prev は先ほどまでの current
	// このプロセスのコンテキストにおいて、これまで実行していたプロセス自体を指す
	prev->state = TASK_RUNNABLE;
	prev->cpuid = -1;
	next->state = TASK_RUNNING;
	next->cpuid = cpuid;
	cpu_switch_to(prev, next);
	// cpu_switch_to したあとここに戻ってきた場合、
	// この prev は少し前に sched を呼んで休止した自分自身のプロセスを指している
	next->state = TASK_RUNNABLE;
	next->cpuid = -1;
	prev->state = TASK_RUNNING;
	prev->cpuid = cpuid;

static char *str[] = {
	"RUNNING",
	"RUNNABLE",
	"ZOMBIE"
};
printf("now 0x%x\n", prev);
for(int i = 0; i < NR_TASKS; i++){
	if (task[i] == 0) continue;
	printf("0x%06x:%d:%s:%d\n", task[i], i, str[task[i]->state], task[i]->cpuid);
}
	// cpu_switch_to で別のプロセスに切り替わったあと、
	// しばらくしてまたこのプロセスのコンテキストに戻ってきた場合は、
	// 切り替えが成功していたとして 1 を返す
	return 1;
}

void schedule_tail(void) {
	preempt_enable();
}


void timer_tick()
{
	int cpuid = get_cpuid();

	--currents[cpuid]->counter;
	if (currents[cpuid]->counter>0 || currents[cpuid]->preempt_count >0) {
		return;
	}
	currents[cpuid]->counter=0;
	enable_irq();
	_schedule();
	disable_irq();
}

void exit_process(){
	int cpuid = get_cpuid();

	preempt_disable();
	for (int i = 0; i < NR_TASKS; i++){
		if (task[i] == currents[cpuid]) {
			task[i]->state = TASK_ZOMBIE;
			break;
		}
	}
	preempt_enable();
	schedule();
}
