#ifndef _SCHED_H
#define _SCHED_H

#define THREAD_CPU_CONTEXT			0 		// offset of cpu_context in task_struct

#ifndef __ASSEMBLER__

#define THREAD_SIZE				4096

#define NR_TASKS				64

#define FIRST_TASK task[0]
#define LAST_TASK task[NR_TASKS-1]

#define TASK_RUNNING				0
#define TASK_ZOMBIE				1

#define PF_KTHREAD				0x00000002


extern struct task_struct *current;
extern struct task_struct * task[NR_TASKS];
extern int nr_tasks;

// 控えないといけないレジスタ値を保存する
// プロセスが切り替わるときは必ず cpu_switch_to 関数が呼ばれるため
// ARM ABI により x0-x18 の値は呼び出し側で控えられる
// それ以外のものだけを保存する
struct cpu_context {
	unsigned long x19;
	unsigned long x20;
	unsigned long x21;
	unsigned long x22;
	unsigned long x23;
	unsigned long x24;
	unsigned long x25;
	unsigned long x26;
	unsigned long x27;
	unsigned long x28;
	unsigned long fp;
	unsigned long sp;
	unsigned long pc;
};

struct cpu_sysregs {
	unsigned long sctlr_el1;
	unsigned long sps_el1;
	unsigned long ttbr0_el1;
	unsigned long ttbr1_el1;
	unsigned long tcr_el1;
	unsigned long mair_el1;
};

#define MAX_PROCESS_PAGES			16

struct user_page {
	unsigned long phys_addr;
	unsigned long virt_addr;
};

struct mm_struct {
	unsigned long first_table;
	// 今使っているユーザプロセス用ページの数
	int user_pages_count;
	// 使っているユーザプロセス用ページのリスト
	struct user_page user_pages[MAX_PROCESS_PAGES];
	// 今使っているカーネル用ページの数
	int kernel_pages_count;
	// 使っているカーネル用ページのオフセットのリスト
	// カーネルが使えるページ数は最大で MAX_PROCESS_PAGES(16) 個まで
	unsigned long kernel_pages[MAX_PROCESS_PAGES];
};

struct task_struct {
	struct cpu_context cpu_context;	// CPU 状態
	long state;						// プロセスの状態(TASK_RUNNING とか)
	long counter;					// プロセスがどれくらい実行されるかを保持
									// tick ごとに 1 減り、0 になると他のプロセスに切り替わる
	long priority;					// タスクがスケジュールされるときにこの値が counter にコピーされる
	long preempt_count;				// 0 以外の値が入っている場合はタスク切り替えが無視される
	long pid;						// VMID
	unsigned long flags;
	struct mm_struct mm;
	struct cpu_sysregs cpu_sysregs;
};

extern void sched_init(void);
extern void schedule(void);
extern void timer_tick(void);
extern void preempt_disable(void);
extern void preempt_enable(void);
extern void set_cpu_sysregs(struct task_struct *task);
extern void switch_to(struct task_struct* next);
extern void cpu_switch_to(struct task_struct* prev, struct task_struct* next);
extern void exit_task(void);

// kernel_main の task_struct の初期値
#define INIT_TASK \
/*cpu_context*/ { { 0,0,0,0,0,0,0,0,0,0,0,0,0}, \
/* state etc */	 0,0,15, 0, 0, PF_KTHREAD, \
/* mm */ { 0, 0, {{0}}, 0, {0}}, \
/* cpu_sysregs */ { 0,0,0,0,0,0}, \
}
#endif
#endif
