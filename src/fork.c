#include "mm.h"
#include "sched.h"
#include "fork.h"
#include "utils.h"
#include "entry.h"

int copy_process(unsigned long clone_flags, unsigned long fn, unsigned long arg)
{
	// copy_process の処理中はスケジューラによるタスク切り替えを禁止
	preempt_disable();
	struct task_struct *p;

	// 新たなページを確保
	unsigned long page = allocate_kernel_page();
	p = (struct task_struct *) page;
	// ページの末尾を pt_regs 用の領域とする
	struct pt_regs *childregs = task_pt_regs(p);

	if (!p)
		return -1;

	if (clone_flags & PF_KTHREAD) {
		// カーネルスレッドの場合、実行する関数名と引数を覚える
		p->cpu_context.x19 = fn;
		p->cpu_context.x20 = arg;
	} else {
		// 今実行中のプロセスのレジスタの保存先を取得
		struct pt_regs * cur_regs = task_pt_regs(current);
		// 構造体の値をコピー
		*childregs = *cur_regs;
		// pt_regs の最初のレジスタの値(x0)を 0 にする
		childregs->regs[0] = 0;
		// メモリ空間を丸ごとコピー
		copy_virt_memory(p);
	}
	// フラグは指定されたものを使い、それ以外は現在のプロセスのものをコピー
	p->flags = clone_flags;
	p->priority = current->priority;
	p->state = TASK_RUNNING;
	p->counter = p->priority;
	p->preempt_count = 1; //disable preemtion until schedule_tail

	// コピーされたプロセスは最初 ret_from_fork 関数から動き出す
	p->cpu_context.pc = (unsigned long)ret_from_fork;
	// ret_from_fork の中で kernel_exit が呼ばれる
	// そのとき SP が指す先には退避したレジスタが格納されている必要がある
	p->cpu_context.sp = (unsigned long)childregs;
	// 今動いているタスク数を増やし、その連番をそのまま PID とする
	int pid = nr_tasks++;
	// 新たに作った task_struct 構造体のアドレスを task 配列に入れておく
	task[pid] = p;	

	preempt_enable();
	return pid;
}

// カーネルのプロセスをユーザプロセスに移す
int move_to_user_mode(unsigned long start, unsigned long size, unsigned long pc)
{
	struct pt_regs *regs = task_pt_regs(current);
	regs->pstate = PSR_MODE_EL0t;
	regs->pc = pc;
	// todo: このスタックポインタへの代入はどういう意味？
	regs->sp = 2 *  PAGE_SIZE;  
	// 新たにページを確保、code_page は仮想アドレス
	unsigned long code_page = allocate_user_page(current, 0);
	if (code_page == 0)	{
		return -1;
	}
	// プログラム全体を code_page にコピー
	memcpy(code_page, start, size);
	// アドレス空間の切り替え(カーネルのアドレス空間 → プロセスのアドレス空間)
	set_pgd(current->mm.pgd);
	return 0;
}

// 指定されたタスクに対応するレジスタの保存先を返す
struct pt_regs * task_pt_regs(struct task_struct *tsk)
{
	// todo: THREAD_SIZE の意味は…？ページのサイズと同じなのはたまたま？
	// ページのサイズが 4096 なので、ページ末尾に pt_regs 用の領域を確保する
	unsigned long p = (unsigned long)tsk + THREAD_SIZE - sizeof(struct pt_regs);
	return (struct pt_regs *)p;
}
