#ifndef _SCHED_H
#define _SCHED_H

#define THREAD_CPU_CONTEXT			0	// offset of cpu_context in vm_struct

#ifndef __ASSEMBLER__

#include "spinlock.h"
#include "loader.h"

#define THREAD_SIZE     4096
#define NUMBER_OF_VMS   64

enum VM_STATE {
    VM_RUNNING = 0,
    VM_RUNNABLE,
    VM_ZOMBIE,
};

struct board_ops;

extern struct vm_struct *vms[NUMBER_OF_VMS];
extern int current_number_of_vms;

// 控えないといけないレジスタ値を保存する
// vm が切り替わるときは必ず cpu_switch_to 関数が呼ばれるため
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
    unsigned long elr_el1;
    unsigned long fpcr;
    unsigned long fpsr;
    unsigned long midr_el1;     // todo: vpidr_el2 では？
    unsigned long mpidr_el1;    // todo: vmpidr_el2 では？
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
    unsigned long id_isar5_el1;     // r
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
    unsigned long csselr_el1;       // rw

    // HCR_EL2.TID1 がセットされている場合にトラップされる
    unsigned long aidr_el1;         // r
    unsigned long revidr_el1;       // r

    // system timer
    // physical timers
    unsigned long cntkctl_el1;
    unsigned long cntp_ctl_el0;
    unsigned long cntp_cval_el0;
    unsigned long cntp_tval_el0;

    // virtual timers
    unsigned long cntv_ctl_el0;
    unsigned long cntv_cval_el0;
    unsigned long cntv_tval_el0;
};

struct mm_struct {
    unsigned long first_table;      // VM の Stage2 変換テーブル
    int vm_pages_count;           // 今使っている VM 用ページの数
    int kernel_pages_count;         // 今使っているカーネル用ページの数
};

struct vm_stat {
    long wfx_trap_count;            // VM が wfi/wfe を実行した回数
    long hvc_trap_count;            // VM がハイパーコールを実行した回数
    long sysregs_trap_count;        // VM が sysregs にアクセスした回数
    long pf_trap_count;             // VM がページフォルトを発生させた回数
    long mmio_trap_count;           // VM が mmio 領域にアクセスした回数
};

struct vm_console {
    struct fifo *in_fifo;
    struct fifo *out_fifo;
};

struct vm_struct {
    // cpu_context はアセンブラで位置指定でアクセスされるので、構造体の先頭に置く
    // THREAD_CPU_CONTEXT がアセンブラでのオフセット
    struct cpu_context cpu_context;	            // CPU 状態
    long state;                                 // VM の状態(VM_RUNNING, VM_ZOMBIE)

    // todo: 今のスケジューラでは使っていない
    long counter;                               // VM が使える残りの CPU 時間を保持
                                                // tick ごとに 1 減り、0 になると他の VM に切り替わる
    // todo: 今のスケジューラでは使っていない
    long priority;                              // VM が CPU にスケジュールされるときにこの値が counter にコピーされる

    long vmid;                                  // VMID
    unsigned long flags;
    const char *name;
    const struct board_ops *board_ops;
    void *board_data;
    struct mm_struct mm;
    struct cpu_sysregs cpu_sysregs;
    struct vm_stat stat;
    struct vm_console console;
    struct spinlock lock;
    struct loader_args loader_args;	            // ローダの引数
};

void sched_init(void);
void timer_tick(void);
void set_cpu_virtual_interrupt(struct vm_struct *);
void set_cpu_sysregs(struct vm_struct *);
void switch_to(struct vm_struct*);
void cpu_switch_to(struct vm_struct* prev, struct vm_struct* next);
void exit_vm(void);
void show_vm_list(void);

void yield();
void scheduler(unsigned long);

#endif
#endif
