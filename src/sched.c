#include "sched.h"
#include "irq.h"
#include "utils.h"
#include "mm.h"
#include "debug.h"
#include "board.h"
#include "vm.h"
#include "cpu_core.h"

static struct vm_struct init_vm = {
	.cpu_context = {0},
	.state       = 0,
	.counter     = 0,
	// todo: priority を大きくしていけば複数の CPU コアで実行されるようになる
	.priority    = 5,
	.vmid        = 0,
	.flags       = 0,
	.name        = "",
	.board_ops   = 0,
	.board_data  = 0,
	.mm          = {0},
	.cpu_sysregs = {0},
	.stat        = {0},
	.console     = {0},
	.lock        = {0, 0, -1},
};

static struct vm_struct idle_vms[NUMBER_OF_CPU_CORES];
// idle vm や動的に作られた vm などへの参照を保持する配列
// todo: 直接触らせない
struct vm_struct *vms[NUMBER_OF_VMS];

// 各 CPU コアで実行中の VM
// todo: CPU 構造体に入れたい
struct vm_struct *currents[NUMBER_OF_CPU_CORES];

// 現在実行中の VM の数(idle_vms があるので初期値は NUMBER_OF_CPU_CORES)
int current_number_of_vms = NUMBER_OF_CPU_CORES;

void initiate_idle_vms() {
	for (int i = 0; i < NUMBER_OF_CPU_CORES; i++) {
		currents[i] = &idle_vms[i];
		memcpy(&idle_vms[i], &init_vm, sizeof(struct vm_struct));

		vms[i] = &idle_vms[i];
		vms[i]->name = "IDLE";
		vms[i]->vmid = i;
		vms[i]->state = VM_ZOMBIE;

		// IDLE VM 用の ホスト用のコンソールの初期化
		init_vm_console(vms[i]);
	}
}

// 現在実行中の VM の vm_struct
struct vm_struct *current_vm() {
	return currents[get_cpuid()];
}

void set_current_vm(struct vm_struct *vm) {
	currents[get_cpuid()] = vm;
}

// VM 切換え
// 複数の CPU が同時に呼び出すのでスレッドセーフにしないといけない
// todo: 複数 CPU で動かす場合は、停止時と異なる CPU で VM が動くかもしれない
static void _schedule(void)
{
	int next, c;
	struct vm_struct *vm;
	unsigned long cpuid = get_cpuid();

	while (1) {
		c = -1;
		next = 0;
		// VM の最大数は決め打ちで NUMBER_OF_VMS 個
		// 先頭から順番に状態を見ていく
		// todo: 積極的に idle vm を選ぶ理由がないので外す
		// for (int i = 0; i < NUMBER_OF_VMS; i++){
		for (int i = NUMBER_OF_CPU_CORES; i < NUMBER_OF_VMS; i++) {
			vm = vms[i];

			// idle_vm のための特別対応、他の vCPU が idle_vm を実行してはいけない
			if (vm->vmid < NUMBER_OF_CPU_CORES && vm->vmid != cpuid) {
				continue;
			}

			acquire_lock(&vm->lock);

			// RUNNING/RUNNABLE 状態で、かつ一番カウンタが大きいものを探す
			// if (vm && vm->state != VM_ZOMBIE && vm->counter >= c) {
			if (vm && vm->state == VM_RUNNABLE && vm->counter >= c) {
				c = vm->counter;
				next = i;
			}

			release_lock(&vm->lock);
		}
		// まだ実行時間(counter)が残っているものがあったらそれを実行する
		if (c) {
			break;
		}

		// すべての VM が実行時間を使い切っていたら、全 VM に実行時間を補充する
		for (int i = 0; i < NUMBER_OF_VMS; i++) {
			vm = vms[i];

			acquire_lock(&vm->lock);

			if (vm) {
				// 何回もループした場合にカウンタの値が大きくなりすぎないように
				// 今のカウンタの値を半分にして、プライオリティを足したもので更新
				vm->counter = (vm->counter >> 1) + vm->priority;
			}

			release_lock(&vm->lock);
		}
		// VM_RUNNING 状態のものが見つかるまでずっとループする
		// 割込みを有効にしておかないと誰も VM の状態を変更できず無限ループになってしまうので
		// timer_tick で割込みを有効にしておく必要がある
		// ただし、割込みは許可されるが preemption(タスク(VM)切り替え)は許可されていないことに注意
	}

	// 切り替え先 VM に switch_to する
// INFO("switch: %d->%d", current()->vmid, next);
	switch_to(vms[next]);

	// todo: xv6 と同じように、タスク間で直接切り替えるのではなく、一度 idle_vm を経由するようにする
	//       各 vm から cpu ごとの idle_vm に戻るための関数 yield を用意する
}

// 自主的に CPU 時間を手放し VM を切り替える
// todo: schedule を呼ぶ前に手動で IRQ を無効化する必要があり危険
//       現状 schedule を呼ぶのは、main, handle_trap_wfx, exit_vm のみ
//       main は手動で無効化している
//       handle_trap_wfx は割込から呼ばれるので、割込みは無効化されている
//       exit_vm は PANIC マクロから呼ばれる、割込みが無効かどうかわからず危ないのでは
void schedule(void)
{
	// 自主的に CPU を手放した場合はカウンタを 0 にする
	current_vm()->counter = 0;
	_schedule();
}

void set_cpu_virtual_interrupt(struct vm_struct *tsk) {
	// もし current の VM に対して irq が発生していたら、仮想割込みを設定する
	if (HAVE_FUNC(tsk->board_ops, is_irq_asserted) && tsk->board_ops->is_irq_asserted(tsk)) {
		assert_virq();
	}
	else {
		clear_virq();
	}

	// fiq も同様
	if (HAVE_FUNC(tsk->board_ops, is_fiq_asserted) && tsk->board_ops->is_fiq_asserted(tsk)) {
		assert_vfiq();
	}
	else {
		clear_vfiq();
	}

	// todo: vserror は？
}

// 指定した VM に切り替える
void switch_to(struct vm_struct * next)
{
	struct vm_struct *current = current_vm();

	if (current == next) {
		return;
	}

	struct vm_struct * prev = current;
	set_current_vm(next);

	// 他のコアで同時に実行してしまわないように VM の状態を切り替える
	// todo: 排他が必要と思われる
	prev->state = VM_RUNNABLE;
	next->state = VM_RUNNING;

	// レジスタを控えて実際に VM を切り替える
	// 戻ってくるときは別の VM になっている
	cpu_switch_to(prev, next);
}

// タイマが発火すると呼ばれ、VM 切り替えを行う
void timer_tick()
{
	struct vm_struct *vm = current_vm();

	--vm->counter;
	// まだ VM が十分な時間実行されていなかったら切り替えずに終了
	if (vm->counter > 0) {
		return;
	}
	vm->counter = 0;

	// 割込みハンドラは割込み無効状態で開始される
	// _schedule 関数の処理中に割込みを使う部分があるので割込みを有効にする
	// todo: なぜ無効のままでよくなった？
	//enable_irq();
	_schedule();
	//disable_irq();
}

void exit_vm(){
	for (int i = 0; i < NUMBER_OF_VMS; i++){
		if (vms[i] == current_vm()) {
			// 実行中の VM の構造体を見つけて zombie にする(=スケジューリング対象から外れる)
			// todo: メモリは解放しなくていい？
			vms[i]->state = VM_ZOMBIE;
			break;
		}
	}

	schedule();
}

void set_cpu_sysregs(struct vm_struct *tsk) {
	set_stage2_pgd(tsk->mm.first_table, tsk->vmid);
	restore_sysregs(&tsk->cpu_sysregs);
}

// ハイパーバイザでの処理を終えて VM に処理を戻すときに kernel_exit から呼ばれる
void vm_entering_work() {
	struct vm_struct *vm = current_vm();

	if (HAVE_FUNC(vm->board_ops, entering_vm)) {
		vm->board_ops->entering_vm(vm);
	}

	// VM 処理に復帰するとき、コンソールがこの VM に紐づいていたら
	// キューに入っていた値を全部出力する
	if (is_uart_forwarded_vm(vm)) {
		flush_vm_console(vm);
	}

	// todo: entering_vm, flush, set_cpu_sysregs, set_cpu_virtual_interrupt の正しい呼び出し順がわからない
	// 控えておいたレジスタの値を戻す
	set_cpu_sysregs(vm);

	// 今実行を再開しようとしている VM に対し仮想割込みを設定する
	//   ハイパーバイザ環境では VM に対し割込みを発生させる必要があるので
	//   VM が実行開始するタイミングで仮想割込みを生成しないといけない
	set_cpu_virtual_interrupt(vm);
}

// VM での処理を抜けてハイパーバイザに処理に入るときに kernel_entry から呼ばれる
void vm_leaving_work() {
	struct vm_struct *vm = current_vm();

	// 今のレジスタの値を控える
	save_sysregs(&vm->cpu_sysregs);

	if (HAVE_FUNC(vm->board_ops, leaving_vm)) {
		vm->board_ops->leaving_vm(vm);
	}

	if (is_uart_forwarded_vm(vm)) {
		flush_vm_console(vm);
	}
}

const char *vm_state_str[] = {
	"RUNNING",
	"RUNNABLE",
	"ZOMBIE",
};

// todo: cpu 構造体に vm 情報を入れたら不要になるはず
int find_cpu_which_runs(struct vm_struct *vm) {
	for (int i = 0; i < NUMBER_OF_CPU_CORES; i++) {
		if (currents[i] == vm) {
			return i;
		}
	}
	return -1;
}

// todo: VM に割り当てられた CPU ID も表示したい
void show_vm_list() {
    printf("  %4s %3s %12s %8s %7s %9s %7s %7s %7s %7s %7s\n",
		   "vmid", "cpu", "name", "state", "pages", "saved-pc", "wfx", "hvc", "sysregs", "pf", "mmio");
    for (int i = 0; i < current_number_of_vms; i++) {
        struct vm_struct *vm = vms[i];
		int cpuid = find_cpu_which_runs(vm);
        printf("%c %4d   %c %12s %8s %7d %9x %7d %7d %7d %7d %7d\n",
               is_uart_forwarded_vm(vms[i]) ? '*' : ' ',
			   vm->vmid,
			   // CPUID は1桁のみ対応
			   (cpuid < 0 || vm->state == VM_ZOMBIE? '-' : '0' + cpuid),
			   vm->name ? vm->name : "",
               vm_state_str[vm->state],
			   vm->mm.vm_pages_count,
			   vm_pt_regs(vm)->pc,
               vm->stat.wfx_trap_count,
			   vm->stat.hvc_trap_count,
               vm->stat.sysregs_trap_count,
			   vm->stat.pf_trap_count,
               vm->stat.mmio_trap_count);
    }
}
