#ifndef _SCHED_H
#define _SCHED_H

#define THREAD_CPU_CONTEXT			0	// offset of cpu_context in task_struct

#ifndef __ASSEMBLER__

#define THREAD_SIZE                	4096

#define NR_TASKS                	64

#define FIRST_TASK task[0]
#define LAST_TASK task[NR_TASKS-1]

#define TASK_RUNNING                0
#define TASK_ZOMBIE                	1

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
    // EL0/1 でアクセスしてもトラップされないレジスタ
    // 切り替え時にレジスタが退避・復帰される
    unsigned long sctlr_el1;
    unsigned long ttbr0_el1;
    unsigned long ttbr1_el1;
    unsigned long tcr_el1;
    unsigned long esr_el1;
    unsigned long far_el1;
    unsigned long afsr0_el1;
    unsigned long afsr1_el1;
    unsigned long mair_el1;
    unsigned long amair_el1;
    unsigned long contextidr_el1;

    unsigned long cpacr_el1;
    unsigned long dczid_el0;
    unsigned long elr_el1;
    unsigned long fpcr;
    unsigned long fpsr;
    unsigned long isr_el1;
    unsigned long midr_el1;
    unsigned long mpidr_el1;
    unsigned long par_el1;
    unsigned long sp_el0;
    unsigned long sp_el1;
    unsigned long spsr_el1;
    unsigned long tpidr_el0;
    unsigned long tpidr_el1;
    unsigned long tpidrro_el0;
    unsigned long vbar_el1;

    // HCR_EL2.TACR がセットされている場合にトラップされる
    unsigned long actlr_el1;        // rw

    // HCR_EL2.TID3 がセットされている場合にトラップされる
    unsigned long id_pfr0_el1;      // r
    unsigned long id_pfr1_el1;      // r
    unsigned long id_mmfr0_el1;     // r
    unsigned long id_mmfr1_el1;     // r
    unsigned long id_mmfr2_el1;     // r
    unsigned long id_mmfr3_el1;     // r
    unsigned long id_isar0_el1;     // r
    unsigned long id_isar1_el1;     // r
    unsigned long id_isar2_el1;     // r
    unsigned long id_isar3_el1;     // r
    unsigned long id_isar4_el1;     // r
    unsigned long id_dsar5_el1;     // r
    unsigned long mvfr0_el1;        // r
    unsigned long mvfr1_el1;        // r
    unsigned long mvfr2_el1;        // r
    unsigned long id_aa64pfr0_el1;  // r
    unsigned long id_aa64pfr1_el1;  // r
    unsigned long id_aa64dfr0_el1;  // r
    unsigned long id_aa64dfr1_el1;  // r
    unsigned long id_aa64isar0_el1; // r
    unsigned long id_aa64isar1_el1; // r
    unsigned long id_aa64mmfr0_el1; // r
    unsigned long id_aa64mmfr1_el1; // r
    unsigned long id_aa64afr0_el1;  // r
    unsigned long id_aa64afr1_el1;  // r

    // HCR_EL2.TID2 がセットされている場合にトラップされる
    unsigned long ctr_el0;          // r
    unsigned long ccsidr_el1;       // r
    unsigned long clidr_el1;        // r
    unsigned long csselrel1;        // rw

    // HCR_EL2.TID1 がセットされている場合にトラップされる
    unsigned long aidr_el1;         // r
    unsigned long revidr_el1;       // r

    // system timer
    unsigned long cntfrq_el0;
    unsigned long cntkctl_el1;
    unsigned long cntp_ctl_el0;
    unsigned long cntp_cval_el0;
    unsigned long cntp_tval_el0;
    unsigned long cntpct_el0;
    unsigned long cntps_ctl_el1;
    unsigned long cntps_cval_el1;
    unsigned long cntps_tval_el1;
    unsigned long cntv_ctl_el0;
    unsigned long cntv_cval_el0;
    unsigned long cntv_tval_el0;
    unsigned long cntvct_el0;
};

struct mm_struct {
    unsigned long first_table;
    // 今使っているユーザプロセス用ページの数
    int user_pages_count;
    // 今使っているカーネル用ページの数
    int kernel_pages_count;
};

struct task_struct {
    struct cpu_context cpu_context;	// CPU 状態
    long state;                     // プロセスの状態(TASK_RUNNING とか)
    long counter;                   // プロセスがどれくらい実行されるかを保持
                                    // tick ごとに 1 減り、0 になると他のプロセスに切り替わる
    long priority;                  // タスクがスケジュールされるときにこの値が counter にコピーされる
    long preempt_count;             // 0 以外の値が入っている場合はタスク切り替えが無視される
    long pid;                       // VMID
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
/*cpu_context*/ { {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, \
/* state etc */   0, 0, 15, 0, 0, 0, \
/* mm */          {0, 0, 0}, \
/* cpu_sysregs */ {0, 0, 0, 0, 0, 0}, \
}
#endif
#endif
