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

// 各スレッド用の領域の末尾に置かれた vcpu_struct へのポインタを返す
struct pt_regs * vcpu_pt_regs(struct vcpu_struct *vcpu) {
	unsigned long p = (unsigned long)vcpu + THREAD_SIZE - sizeof(struct pt_regs);
	return (struct pt_regs *)p;
}

// idle vcpu 用のなにもしないコード
static void idle_loop() {
	while (1) {
		// todo: CPU を無駄に使わないようにしたい
		//       ゲストが wfi を実行しても、トラップされて他の VM が動き出してしまう
		// asm volatile("wfi");
	}
}

// vCPU の初期状態を設定する共通処理(vCPU に切り替え後に呼ばれる)
static void start_vcpu() {
	struct vcpu_struct *vcpu = current_pcpu()->current_vcpu;

	if (!vcpu) {
		PANIC("vCPU is NULL");
	}

	// vCPU の切り替え前に必ずロックしているので、まずそれを解除する
	release_lock(&vcpu->lock);

	// PSTATE の中身は SPSR レジスタに戻したうえで eret することで復元される
	// ここで設定した regs->pstate は restore_sysregs で SPSR に戻される
	// その後 kernel_exit で eret され実際のレジスタに復元される
	struct pt_regs *regs = vcpu_pt_regs(vcpu);
	regs->pstate = PSR_MODE_EL1h;	// EL を1、使用する SP を SP_EL1 にする
	regs->pstate |= (0xf << 6);		// DAIF をすべて1にする、つまり全ての例外をマスクしている

	set_cpu_sysregs(vcpu);

	INFO("%s enters EL1...", vcpu->vm->name);
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

static void init_vm_console(struct vm_struct2 *vm) {
	vm->console.in_fifo = create_fifo();
	vm->console.out_fifo = create_fifo();
}

void increment_current_pc(int ilen) {
	struct pt_regs *regs = vcpu_pt_regs(current_pcpu()->current_vcpu);
	regs->pc += ilen;
}

// 空の vCPU 構造体を作成
// あとでこの vCPU に CPU 時間が割当たるとロードなどが行われる
static struct vcpu_struct *create_vcpu(unsigned long vcpuid) {
	struct vcpu_struct *vcpu;

	// このページはハイパーバイザが使う管理用(ゲストから復帰してきたときのスタック領域を含む)
	// allocate_page は確保したページをゼロクリアして返す
	// vcpu 構造体の初期化はやっていないが、ゼロクリアされているので結果的に問題ない
	// todo: 明示的な初期化処理としたほうがよい
	unsigned long page = allocate_page();
	// ページの先頭に vcpu_struct を置く(ゲストの管理用データ置き場)
	vcpu = (struct vcpu_struct *) page;
	// ページの末尾を pt_regs 用の領域とする(ゲストのレジスタ保存用)
	struct pt_regs *childregs = vcpu_pt_regs(vcpu);

	if (!vcpu) {
		WARN("Failed to allocate page for vCPU");
		return NULL;
	}

	// vcpu->flags = 0;
	// vcpu->priority = current_pcpu()->current_vcpu->priority;
	// vcpu->counter = vcpu->priority;
	vcpu->state = VCPU_RUNNABLE;
	vcpu->vcpu_id = vcpuid;

	// todo: 最初に pCPU が割当たるときに releaselock するから 1 にしたが、いいのか？
	vcpu->interrupt_enable = 1;
	vcpu->number_of_off = 1;

	// EL1 に復帰するときにシステムレジスタ、汎用レジスタに書き戻される値の初期化
	prepare_initial_sysregs();
	memcpy(&vcpu->cpu_sysregs, &initial_sysregs, sizeof(struct cpu_sysregs));

	// vCPU の CPU ID を設定
	vcpu->cpu_sysregs.mpidr_el1 = 0x80000000 | vcpuid;
	set_vmpidr_el2(0x80000000 | vcpuid);

	// el1 で動くゲスト OS カーネルは、最初は switch_from_kthread 関数から動き出す
	vcpu->cpu_context.pc = (unsigned long)switch_from_kthread;

	// switch_from_kthread の中で kernel_exit が呼ばれる
	// そのとき SP が指す先には退避したレジスタが格納されている必要がある
	// SP レジスタは EL によってバンクされており、このスタックポインタはあくまで
	// ハイパーバイザ(EL2)が処理するときに使うもの(SP_EL2)である
	// ゲスト OS の SP は SP_EL1 もしくは SP_EL0 であり、別になっている
	// (実際ゲスト OS の SP は loader_args 内で指定されている)
	// よって、ゲストからハイパーバイザに戻った時の処理を1ページ分のスタックでやりきれるなら
	// 特に問題はない(ゲスト OS 自体は自由に自分で確保したスタックを使える)
	vcpu->cpu_context.sp = (unsigned long)childregs;

	return vcpu;
}

// 指定された CPU コア用の IDLE VM を作る(create_vm_with_loader と同様の処理を行う)
int create_idle_vm() {
	// todo: idle VM はなくしたい…
	unsigned long page = allocate_page();
	if (!page) {
		WARN("Failed to allocate page for idle VM");
		return -1;
	}
	struct vm_struct2 *idle_vm = (struct vm_struct2 *)page;

	idle_vm->name = "IDLE";
	init_vm_console(idle_vm);
	init_lock(&idle_vm->lock, "idle_vm_lock");

	int vmid = 0;
	vms2[vmid] = idle_vm;
	idle_vm->vmid = vmid;
	
	unsigned long pc = 0x0;
	unsigned long sp = 0x100000;

	for (int i=0; i < NUMBER_OF_PCPUS; i++) {
		struct vcpu_struct *idle_vcpu = create_vcpu(i);
		if (!idle_vcpu) {
			WARN("Failed to allocate page for vCPU");
			return -1;
		}

		idle_vcpu->vm = idle_vm;
		init_lock(&idle_vcpu->lock, "vcpu_lock");

		if (i == 0) {
			// 指定したアドレスに格納されたテキストコードを VM 領域のアドレス 0 にコピーする
			copy_code_to_memory(idle_vcpu, 0, (unsigned long)idle_loop, PAGE_SIZE);
		}

		struct pt_regs *regs = vcpu_pt_regs(idle_vcpu);
		regs->pc = pc;
		regs->sp = sp;

		idle_vcpu->cpu_context.x19 = (unsigned long)start_vcpu;

		int vcpuid = i;
		vcpus[vcpuid] = idle_vcpu;
	}

	return vmid;
}

// todo:
// ここで vcpus にエントリを追加している
// ひとつの VM に複数の vCPU を割当てるなら、ここで対応が必要
// つまり、ここで必要なだけの vCPU を作成する
// vCPU でメモリ空間は共通だから、OS テキストを複数回ロードする必要はない
// 今は作った vCPU が最初に動くときにロードしているので、このままだと複数回ロードしてしまう
// また vcpu の管理領域は各 vcpu に固有でいいが、vm の管理領域は共通にしないといけない
// vcpu の管理領域は、この関数の先頭の create_vcpu で確保されている

// todo: リファクタリング必要

// 指定されたローダで VM を作る
int create_vm_with_loader(loader_func_t loader, void *arg) {
	// vCPU に共通の VM の管理用構造体を確保する
	unsigned long page = allocate_page();
	if (!page) {
		WARN("Failed to allocate page for VM");
		return -1;
	}
	struct vm_struct2 *vm = (struct vm_struct2 *)page;

	// VM の初期化
	vm->loader_args = *(struct loader_args *)arg;	// ローダの引数をコピー
	// todo: vm 名の表示が壊れる(メモリが解放されている？)
	vm->name = vm->loader_args.filename;

	init_vm_console(vm);
	init_lock(&vm->lock, "vm_lock");

	// VM を管理リストに登録
	int vmid = current_number_of_vms++;
	vms2[vmid] = vm;
	vm->vmid = vmid;

	// todo: vcpu->vm に値を設定している部分は vcpu ではなく vm への設定なので、
	//       ループの中には入れず、1回だけ初期化するようにする
	//       create_vcpu 内でも vcpu->vm に値を設定しているのでそちらも修正する
	//       vcpu->vm が指す先は別途1回だけ page_alloc しておく必要がある

	unsigned long pc;
	unsigned long sp;

	// todo: いったんすべての vm で2コア固定とする
	// todo: vCPU の作りすぎをチェックしていない
	for (int i = 0; i < 2; i++) {
		// 必要なだけ vCPU を準備
		// todo: create_vcpu 内で vm に対する処理を実行しているので取り出してループの外に置く
		struct vcpu_struct *vcpu = create_vcpu(i);
		if (!vcpu) {
			// todo: 途中で失敗した場合は、既に作った vCPU を削除しないといけない
			WARN("Failed to allocate page for vCPU");
			return -1;
		}

		// vCPU が担当する vm を登録
		vcpu->vm = vm;
		init_lock(&vcpu->lock, "vcpu_lock");

		if (i == 0) {
			// ロードは1回だけ実施
			// todo: ループの外に出したい

			// 指定されたローダを使ってファイルからテキストコードをロードし、PC/SP を取得
			if (loader(arg, &pc, &sp, vcpu) < 0) {
				WARN("Failed to load VM image");
				return -1;
			}
		}

		// vCPU の PC/SP を設定
		struct pt_regs *regs = vcpu_pt_regs(vcpu);
		regs->pc = pc;
		regs->sp = sp;

		vcpu->cpu_context.x19 = (unsigned long)start_vcpu;

		// todo: ループの外に出す、いったん初回のみ実行することにして回避
		// この VM で再現するハードウェア(BCM2837)を初期化
		if (i == 0) {
			vm->board_ops = &bcm2837_board_ops;
			if (HAVE_FUNC(vm->board_ops, initialize)) {
				vm->board_ops->initialize(vcpu);
			}
		}

		// 新たに作った vcpu_struct 構造体のアドレスを vcpus 配列に入れておく
		// これでそのうち今作った vCPU に処理が切り替わり、switch_from_kthread から実行開始される
		int vcpuid = current_number_of_vcpus++;
		vcpus[vcpuid] = vcpu;
	}

	return vmid;
}

void flush_vm_console(struct vm_struct2 *vm) {
	struct fifo *outfifo = vm->console.out_fifo;
	unsigned long val;
	while (dequeue_fifo(outfifo, &val) == 0) {
		printf("%c", val);
	}
}
