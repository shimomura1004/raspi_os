#include "mm.h"
#include "sched.h"
#include "task.h"
#include "utils.h"
#include "entry.h"
#include "debug.h"
#include "bcm2837.h"
#include "board.h"
#include "fifo.h"
#include "irq.h"

// 各スレッド用の領域の末尾に置かれた task_struct へのポインタを返す
struct pt_regs * task_pt_regs(struct task_struct *tsk) {
	unsigned long p = (unsigned long)tsk + THREAD_SIZE - sizeof(struct pt_regs);
	return (struct pt_regs *)p;
}

static void prepare_task(loader_func_t loader, void *arg) {
	INFO("loading... arg=%ld, EL=%d", arg, get_el());

	// PSTATE の中身は SPSR レジスタに戻したうえで eret することで復元される
	// ここで設定した regs->pstate は restore_sysregs で SPSR に戻される
	// その後 kernel_exit で eret され実際のレジスタに復元される
	struct pt_regs *regs = task_pt_regs(current);
	regs->pstate = PSR_MODE_EL1h;	// EL を1、使用する SP を SP_EL1 にする
	regs->pstate |= (0xf << 6);		// DAIF をすべて1にする、つまり全ての例外をマスクしている

	if (loader(arg, &regs->pc, &regs->sp) < 0) {
		PANIC("failed to load");
	}

	set_cpu_sysregs(current);

	INFO("entering el1...");
}

static struct cpu_sysregs initial_sysregs;

static void prepare_initial_sysregs(void) {
	static int is_first_call = 1;

	if (!is_first_call) {
		return;
	}

	// 初回のみ sysregs の値(つまり初期値)を控える
	get_all_sysregs(&initial_sysregs);

	// MMU を確実に無効化する(初期値に関わらず 0 ビット目を確実に 0 にする)
	// SCTLR_EL1 の 0 ビット目は M ビット
	// M bit: MMU enable for EL1&0 stage 1 address translation.
	//        0b0/0b1: EL1&0 stage 1 address translation disabled/enabled
	initial_sysregs.sctlr_el1 &= ~1;

	is_first_call = 0;
}

void increment_current_pc(int ilen) {
	struct pt_regs *regs = task_pt_regs(current);
	regs->pc += ilen;
}

// EL2 で動くタスク(=VM)を作る
int create_task(loader_func_t loader, void *arg) {
	struct task_struct *p;

	// 新たなページを確保
	unsigned long page = allocate_page();
	// ページの先頭に task_struct を置く
	p = (struct task_struct *) page;
	// ページの末尾を pt_regs 用の領域とする
	struct pt_regs *childregs = task_pt_regs(p);

	if (!p) {
		return -1;
	}

	// switch_from_kthread 内で x19 のアドレスにジャンプする
	p->cpu_context.x19 = (unsigned long)prepare_task;
	p->cpu_context.x20 = (unsigned long)loader;
	p->cpu_context.x21 = (unsigned long)arg;
	p->flags = 0;

	p->priority = current->priority;
	p->state = TASK_RUNNING;
	p->counter = p->priority;
	p->name = "VM";

	// このプロセス(vm)で再現するハードウェア(BCM2837)を初期化
	p->board_ops = &bcm2837_board_ops;
	if (HAVE_FUNC(p->board_ops, initialize)) {
		p->board_ops->initialize(p);
	}

	prepare_initial_sysregs();
	memcpy(&p->cpu_sysregs, &initial_sysregs, sizeof(struct cpu_sysregs));

	// el1 のプロセスは最初 switch_from_kthread 関数から動き出す
	p->cpu_context.pc = (unsigned long)switch_from_kthread;
	// switch_from_kthread の中で kernel_exit が呼ばれる
	// そのとき SP が指す先には退避したレジスタが格納されている必要がある
	p->cpu_context.sp = (unsigned long)childregs;
	// 今動いているタスク数を増やし、その連番をそのまま PID とする
	int pid = nr_tasks++;
	// 新たに作った task_struct 構造体のアドレスを task 配列に入れておく
	// これでそのうち今作ったタスクに処理が切り替わり、switch_from_kthread から実行開始される
	task[pid] = p;
	p->pid = pid;

	init_task_console(p);

	return pid;
}

void init_task_console(struct task_struct *tsk) {
	tsk->console.in_fifo = create_fifo();
	tsk->console.out_fifo = create_fifo();
}

void flush_task_console(struct task_struct *tsk) {
	struct fifo *outfifo = tsk->console.out_fifo;
	unsigned long val;
	while (dequeue_fifo(outfifo, &val) == 0) {
		printf("%c", val);
	}
}

void init_initial_task() {
	task[0]->name = "IDLE";
}