#include "mm.h"
#include "sched.h"
#include "vm.h"
#include "utils.h"
#include "entry.h"
#include "debug.h"
#include "bcm2837.h"
#include "board.h"
#include "fifo.h"
#include "irq.h"
#include "loader.h"

// 各スレッド用の領域の末尾に置かれた vm_struct へのポインタを返す
struct pt_regs * vm_pt_regs(struct vm_struct *vm) {
	unsigned long p = (unsigned long)vm + THREAD_SIZE - sizeof(struct pt_regs);
	return (struct pt_regs *)p;
}

static void idle_loop() {
	while (1) {
		asm volatile("wfi");
	}
}

static void prepare_idle_vm() {
	struct vm_struct *vm = current_cpu_core()->current_vm;

	release_lock(&vm->lock);

	struct pt_regs *regs = vm_pt_regs(vm);
	regs->pstate = PSR_MODE_EL1h;	// EL を1、使用する SP を SP_EL1 にする
	regs->pstate |= (0xf << 6);		// DAIF をすべて1にする、つまり全ての例外をマスクしている

	set_cpu_sysregs(vm);

	INFO("%s enters el1...", vm->name);

	copy_code_to_memory(vm, 0, (unsigned long)idle_loop, PAGE_SIZE);
	regs->pc = 0x0;
	regs->sp = 0x100000;
}

static void prepare_vm(loader_func_t loader, void *arg) {
	struct raw_binary_loader_args *loader_args = (struct raw_binary_loader_args *)arg;
	struct vm_struct *vm = current_cpu_core()->current_vm;
	INFO("loading... %s, EL=%d", loader_args->filename, get_el());

	// VM の切り替え前に必ずロックしているので、まずそれを解除する
	release_lock(&vm->lock);

	// PSTATE の中身は SPSR レジスタに戻したうえで eret することで復元される
	// ここで設定した regs->pstate は restore_sysregs で SPSR に戻される
	// その後 kernel_exit で eret され実際のレジスタに復元される
	struct pt_regs *regs = vm_pt_regs(vm);
	regs->pstate = PSR_MODE_EL1h;	// EL を1、使用する SP を SP_EL1 にする
	regs->pstate |= (0xf << 6);		// DAIF をすべて1にする、つまり全ての例外をマスクしている

	if (loader(arg, &regs->pc, &regs->sp) < 0) {
		PANIC("failed to load");
	}

	set_cpu_sysregs(vm);

	INFO("%s enters el1...", vm->name);
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
	struct pt_regs *regs = vm_pt_regs(current_cpu_core()->current_vm);
	regs->pc += ilen;
}

// todo: create_vm/create_idle_vm で処理を共通化する
// 指定された CPU コア用の IDLE VM を作る
int create_idle_vm(unsigned long cpuid) {
	struct vm_struct *vm;

	// 新たなページを確保
	unsigned long page = allocate_page();
	// ページの先頭に vm_struct を置く
	vm = (struct vm_struct *) page;
	// ページの末尾を pt_regs 用の領域とする
	struct pt_regs *childregs = vm_pt_regs(vm);

	if (!vm) {
		return -1;
	}

	// switch_from_kthread 内で x19 のアドレスにジャンプする
	vm->cpu_context.x19 = (unsigned long)prepare_idle_vm;
	// vm->cpu_context.x19 = (unsigned long)prepare_vm;
	// vm->cpu_context.x20 = (unsigned long)loader;
	// vm->cpu_context.x21 = (unsigned long)arg;
	vm->flags = 0;

	vm->priority = current_cpu_core()->current_vm->priority;
	vm->state = VM_RUNNABLE;
	vm->counter = vm->priority;
	vm->name = "IDLE";
	// vm->name = "VM";

	// このプロセス(vm)で再現するハードウェア(BCM2837)を初期化
	vm->board_ops = &bcm2837_board_ops;
	if (HAVE_FUNC(vm->board_ops, initialize)) {
		vm->board_ops->initialize(vm);
	}

	prepare_initial_sysregs();
	memcpy(&vm->cpu_sysregs, &initial_sysregs, sizeof(struct cpu_sysregs));

	// el1 で動くゲスト OS カーネルは、最初は switch_from_kthread 関数から動き出す
	vm->cpu_context.pc = (unsigned long)switch_from_kthread;
	// switch_from_kthread の中で kernel_exit が呼ばれる
	// そのとき SP が指す先には退避したレジスタが格納されている必要がある
	vm->cpu_context.sp = (unsigned long)childregs;
	// IDLE VM は CPU ID をそのまま VMID にする
	int vmid = cpuid;
	// // 今動いている VM 数を増やし、その連番をそのまま PID とする
	// int vmid = current_number_of_vms++;
	// 新たに作った vm_struct 構造体のアドレスを vms 配列に入れておく
	// これでそのうち今作った VM に処理が切り替わり、switch_from_kthread から実行開始される
	vms[vmid] = vm;
	vm->vmid = vmid;

	init_vm_console(vm);

	return vmid;
}

// EL1 で動く VM を作る(この VM に CPU が割り当たるとロードなどが行われる)
int create_vm(loader_func_t loader, void *arg) {
	struct vm_struct *vm;

	// 新たなページを確保
	unsigned long page = allocate_page();
	// ページの先頭に vm_struct を置く
	vm = (struct vm_struct *) page;
	// ページの末尾を pt_regs 用の領域とする
	struct pt_regs *childregs = vm_pt_regs(vm);

	if (!vm) {
		return -1;
	}

	// switch_from_kthread 内で x19 のアドレスにジャンプする
	vm->cpu_context.x19 = (unsigned long)prepare_vm;
	vm->cpu_context.x20 = (unsigned long)loader;
	vm->cpu_context.x21 = (unsigned long)arg;
	vm->flags = 0;

	vm->priority = current_cpu_core()->current_vm->priority;
	vm->state = VM_RUNNABLE;
	vm->counter = vm->priority;
	vm->name = "VM";

	// このプロセス(vm)で再現するハードウェア(BCM2837)を初期化
	vm->board_ops = &bcm2837_board_ops;
	if (HAVE_FUNC(vm->board_ops, initialize)) {
		vm->board_ops->initialize(vm);
	}

	prepare_initial_sysregs();
	memcpy(&vm->cpu_sysregs, &initial_sysregs, sizeof(struct cpu_sysregs));

	// el1 で動くゲスト OS カーネルは、最初は switch_from_kthread 関数から動き出す
	vm->cpu_context.pc = (unsigned long)switch_from_kthread;
	// switch_from_kthread の中で kernel_exit が呼ばれる
	// そのとき SP が指す先には退避したレジスタが格納されている必要がある
	vm->cpu_context.sp = (unsigned long)childregs;
	// 今動いている VM 数を増やし、その連番をそのまま PID とする
	int vmid = current_number_of_vms++;
	// 新たに作った vm_struct 構造体のアドレスを vms 配列に入れておく
	// これでそのうち今作った VM に処理が切り替わり、switch_from_kthread から実行開始される
	vms[vmid] = vm;
	vm->vmid = vmid;

	init_vm_console(vm);

	return vmid;
}

void init_vm_console(struct vm_struct *tsk) {
	tsk->console.in_fifo = create_fifo();
	tsk->console.out_fifo = create_fifo();
}

void flush_vm_console(struct vm_struct *tsk) {
	struct fifo *outfifo = tsk->console.out_fifo;
	unsigned long val;
	while (dequeue_fifo(outfifo, &val) == 0) {
		printf("%c", val);
	}
}
