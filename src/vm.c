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

// idle vm 用のなにもしないコード
static void idle_loop() {
	while (1) {
		// todo: CPU を無駄に使わないようにしたい
		//       ゲストが wfi を実行しても、トラップされて他の VM が動き出してしまう
		// asm volatile("wfi");
	}
}

// VM の初期状態を設定する共通処理
static struct vm_struct *prepare_vm() {
	struct vm_struct *vm = current_cpu_core()->current_vm;

	// VM の切り替え前に必ずロックしているので、まずそれを解除する
	release_lock(&vm->lock);

	// PSTATE の中身は SPSR レジスタに戻したうえで eret することで復元される
	// ここで設定した regs->pstate は restore_sysregs で SPSR に戻される
	// その後 kernel_exit で eret され実際のレジスタに復元される
	struct pt_regs *regs = vm_pt_regs(vm);
	regs->pstate = PSR_MODE_EL1h;	// EL を1、使用する SP を SP_EL1 にする
	regs->pstate |= (0xf << 6);		// DAIF をすべて1にする、つまり全ての例外をマスクしている

	set_cpu_sysregs(vm);

	return vm;
}

// 指定したアドレスに格納されたテキストコードを VM 領域のアドレス 0 にコピーする
static void load_vm_text_from_memory(unsigned long text) {
	struct vm_struct *vm = prepare_vm();
	struct pt_regs *regs = vm_pt_regs(vm);

	// コードをロードして PC/SP を設定
	copy_code_to_memory(vm, 0, text, PAGE_SIZE);
	regs->pc = 0x0;
	regs->sp = 0x100000;

	INFO("%s enters EL1...", vm->name);
}

// 指定されたローダを使い、ファイルからテキストコードをロードする
static void load_vm_text_from_file(loader_func_t loader, void *arg) {
	struct vm_struct *vm = prepare_vm();
	struct pt_regs *regs = vm_pt_regs(vm);

	// コードをロードして PC/SP を設定(pc,sp は出力引数)
	if (loader(arg, &regs->pc, &regs->sp) < 0) {
		PANIC("failed to load");
	}

	INFO("%s enters EL1...", vm->name);
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

// 空の VM 構造体を作成
// あとでこの VM に CPU 時間が割当たるとロードなどが行われる
static struct vm_struct *create_vm() {
	struct vm_struct *vm;

	// 新たなページを確保
	// このページはハイパーバイザが使う管理用(ゲストから復帰してきたときのスタック領域を含む)
	// allocate_page は確保したページをゼロクリアして返す
	// vcpu 構造体の初期化はやっていないが、ゼロクリアされているので結果的に問題ない
	// todo: 明示的な初期化処理としたほうがよい
	unsigned long page = allocate_page();
	// ページの先頭に vm_struct を置く(ゲストの管理用データ置き場)
	vm = (struct vm_struct *) page;
	// ページの末尾を pt_regs 用の領域とする(ゲストのレジスタ保存用)
	struct pt_regs *childregs = vm_pt_regs(vm);

	if (!vm) {
		WARN("Failed to allocate page for vCPU");
		return NULL;
	}

	// vm->flags = 0;
	// vm->priority = current_cpu_core()->current_vm->priority;
	// vm->counter = vm->priority;
	vm->state = VM_RUNNABLE;

	// todo: vcpu ではなく vm への設定なので別の場所に移す
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
	// SP レジスタは EL によってバンクされており、このスタックポインタはあくまで
	// ハイパーバイザ(EL2)が処理するときに使うもの(SP_EL2)である
	// ゲスト OS の SP は SP_EL1 もしくは SP_EL0 であり、別になっている
	// (実際ゲスト OS の SP は loader_args 内で指定されている)
	// よって、ゲストからハイパーバイザに戻った時の処理を1ページ分のスタックでやりきれるなら
	// 特に問題はない(ゲスト OS 自体は自由に自分で確保したスタックを使える)
	vm->cpu_context.sp = (unsigned long)childregs;

	// todo: vcpu ではなく vm への設定なので別の場所に移す
	init_vm_console(vm);

	return vm;
}

// todo: create_vm_with_loader と同様の修正を加える
// 指定された CPU コア用の IDLE VM を作る
int create_idle_vm(unsigned long cpuid) {
	struct vm_struct *vm = create_vm();
	if (!vm) {
		return -1;
	}

	// switch_from_kthread 内で x19 のアドレスにジャンプする
	vm->cpu_context.x19 = (unsigned long)load_vm_text_from_memory;
	vm->cpu_context.x20 = (unsigned long)idle_loop;
	vm->name = "IDLE";

	// IDLE VM は CPU ID をそのまま VMID にする
	int vmid = cpuid;

	// 新たに作った vm_struct 構造体のアドレスを vms 配列に入れておく
	// これでそのうち今作った VM に処理が切り替わり、switch_from_kthread から実行開始される
	vms[vmid] = vm;
	vm->vmid = vmid;

	return vmid;
}

// 指定されたローダで VM を作る
int create_vm_with_loader(loader_func_t loader, void *arg) {
	// todo: ここでループして vcpu を複数作るようにする
	struct vm_struct *vm = create_vm();
	if (!vm) {
		return -1;
	}

	// todo: vcpu->vm に値を設定している部分は vcpu ではなく vm への設定なので、
	//       ループの中には入れず、1回だけ初期化するようにする
	//       create_vcpu 内でも vcpu->vm に値を設定しているのでそちらも修正する
	//       vcpu->vm が指す先は別途1回だけ page_alloc しておく必要がある

	// ローダの引数をコピー
	vm->loader_args = *(struct loader_args *)arg;

	// switch_from_kthread 内で x19 のアドレスにジャンプする
	vm->cpu_context.x19 = (unsigned long)load_vm_text_from_file;
	vm->cpu_context.x20 = (unsigned long)loader;
	vm->cpu_context.x21 = (unsigned long)&vm->loader_args;
	vm->name = "VM";

	// todo:
	// ここで vcpus にエントリを追加している
	// ひとつの VM に複数の vCPU を割当てるなら、ここで対応が必要
	// つまり、ここで必要なだけの vCPU を作成する
	// vCPU でメモリ空間は共通だから、OS テキストを複数回ロードする必要はない
	// 今は作った vCPU が最初に動くときにロードしているので、このままだと複数回ロードしてしまう
	// また vcpu の管理領域は各 vcpu に固有でいいが、vm の管理領域は共通にしないといけない
	// vcpu の管理領域は、この関数の先頭の create_vcpu で確保されている

	// todo: 修正必要(VM 数 != VCPU 数)
	// 今動いている VM 数を増やし、その連番をそのまま PID とする
	int vmid = current_number_of_vms++;
	vms[vmid] = vm;
	vm->vmid = vmid;

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
