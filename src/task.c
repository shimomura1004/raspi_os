#include "mm.h"
#include "sched.h"
#include "task.h"
#include "utils.h"
#include "user.h"
#include "printf.h"
#include "entry.h"

// 各スレッド用の領域の末尾に置かれた task_struct へのポインタを返す
static struct pt_regs * task_pt_regs(struct task_struct *tsk) {
	unsigned long p = (unsigned long)tsk + THREAD_SIZE - sizeof(struct pt_regs);
	return (struct pt_regs *)p;
}

// スレッド構造体に必要なデータをセットする
static int prepare_el1_switching(unsigned long start, unsigned long size, unsigned long pc) {
	struct pt_regs *regs = task_pt_regs(current);
	regs->pstate = PSR_MODE_EL1h;

	// pc は 0 になるが、新しく確保したページを仮想アドレスの 0 にマッピングしているので問題ない
	// todo: el1 に eret したあとにメモリアクセスできなくなるのは別の問題
	regs->pc = pc;
	regs->sp = 2 * PAGE_SIZE;
	unsigned long code_page = allocate_user_page(current, 0);
	if (code_page == 0) {
		return -1;
	}
	memcpy(code_page, start, size);
	set_cpu_sysregs(current);

	unsigned long do_at(unsigned long);
	unsigned long _get_id_aa64mmfr0_el1(void);
	printf("psize=%x\n", _get_id_aa64mmfr0_el1() & 0xf);
	// todo: デバッグ用に、アドレス変換ができるかを確認
	printf("do_at: %x\n", do_at(0));
	while(0);

	return 0;
}

static void prepare_vmtask(unsigned long arg) {
	printf("task: arg=%d, EL=%d\r\n", arg, get_el());

	// デバッグ用に、EL1 で実行する命令の置き場所を変えた
	unsigned int insns[] = {
		0x52800008,	// mov w8, #0
		0xd4000001, // hvc #0
		0xd65f03c0,	// ret
	};
	int err = prepare_el1_switching((unsigned long)insns, (unsigned long)(sizeof(insns)), 0x80000);

	// user_begin/user_end はリンカスクリプトで指定されたアドレス
	// ユーザプログラムのコードやデータ領域の先頭と末尾
	unsigned long begin = (unsigned long)&user_begin;
	unsigned long end = (unsigned long)&user_end;
	unsigned long process = (unsigned long)&user_process;
	// プロセスのアドレス空間内のアドレスを計算して渡す
	int err = prepare_el1_switching(begin, end - begin, process - begin);
	if (err < 0) {
		printf("task: prepare_el1_switching() failed.\r\n");
	}
	printf("task: entering el1...\n");

	// switch_from_kthread() will be called and switched to EL1
	// todo: eret で戻る EL は spsr_el2 に書かれているが、これを準備していないので、
	//       EL1 にならないのでは？
}

static struct cpu_sysregs initial_sysregs;

static void prepare_initial_sysregs(void) {
	static int is_first_call = 1;

	if (!is_first_call) {
		return;
	}

	// 初回のみ sysregs の値(つまり初期値)を控える
	_get_sysregs(&initial_sysregs);

	// MMU を確実に無効化する(初期値に関わらず 0 ビット目を確実に 0 にする)
	// SCTLR_EL1 の 0 ビット目は M ビット
	// M bit: MMU enable for EL1&0 stage 1 address translation.
	//        0b0/0b1: EL1&0 stage 1 address translation disabled/enabled
	initial_sysregs.sctlr_el1 &= ~1;

	is_first_call = 0;
}

// EL2 で動くタスクを作る
int create_vmtask(unsigned long arg)
{
	// copy_process の処理中はスケジューラによるタスク切り替えを禁止
	preempt_disable();
	struct task_struct *p;

	// 新たなページを確保
	unsigned long page = allocate_kernel_page();
	// ページの先頭に task_struct を置く
	p = (struct task_struct *) page;
	// ページの末尾を pt_regs 用の領域とする
	struct pt_regs *childregs = task_pt_regs(p);

	if (!p)
		return -1;

	// switch_from_kthread 内で x19 のアドレスにジャンプする
	p->cpu_context.x19 = (unsigned long)prepare_vmtask;
	p->cpu_context.x20 = arg;
	p->flags = PF_KTHREAD;

	p->priority = current->priority;
	p->state = TASK_RUNNING;
	p->counter = p->priority;
	p->preempt_count = 1; 	// disable preemtion until schedule_tail

	prepare_initial_sysregs();
	memcpy((unsigned long)&p->cpu_sysregs, (unsigned long)&initial_sysregs, sizeof(struct cpu_sysregs));

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

	preempt_enable();
	return pid;
}
