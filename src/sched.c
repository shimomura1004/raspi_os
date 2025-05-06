#include "sched.h"
#include "irq.h"
#include "utils.h"
#include "mm.h"
#include "debug.h"
#include "board.h"
#include "vm.h"
#include "cpu_core.h"
#include "spinlock.h"

// idle vcpu や動的に作られた vcpu などへの参照を保持する配列
// todo: 直接触らせないようにする
// todo: vCPU や VM が終了し、ID が再利用される場合は、単純に VM 数を数えるだけではダメ

// 現在実行中の vCPU の管理リストと保持数(idle_vms があるので初期値は NUMBER_OF_PCPUS)
struct vcpu_struct *vcpus[NUMBER_OF_VCPUS];
int current_number_of_vcpus = NUMBER_OF_PCPUS;

// 現在実行中の VM の管理リストと保持数(idle_vms があるので初期値は 1)
struct vm_struct2 *vms2[NUMBER_OF_VMS];
int current_number_of_vms = 1;

void set_cpu_virtual_interrupt(struct vcpu_struct *vcpu) {
	// もし current の VM に対して irq が発生していたら、仮想割込みを設定する
	if (HAVE_FUNC(vcpu->vm->board_ops, is_irq_asserted) && vcpu->vm->board_ops->is_irq_asserted(vcpu)) {
		assert_virq();
	}
	else {
		clear_virq();
	}

	// fiq も同様
	if (HAVE_FUNC(vcpu->vm->board_ops, is_fiq_asserted) && vcpu->vm->board_ops->is_fiq_asserted(vcpu)) {
		assert_vfiq();
	}
	else {
		clear_vfiq();
	}

	// todo: vserror は？
}

// タイマが発火すると呼ばれ、VM 切り替えを行う
void timer_tick() {
	DEBUG("TICK");
	yield();
}

void exit_vm(){
	// todo: リソースを解放する(コンソールとかメモリページとか)

	// 実行中の VM のstate を zombie にする(=スケジューリング対象から外れる)
	current_pcpu()->current_vcpu->state = VCPU_ZOMBIE;

	// todo: VM 終了が正しく実装できていないので、ここで停止させる
	while(1);

	yield();
}

void set_cpu_sysregs(struct vcpu_struct *vcpu) {
	set_stage2_pgd(vcpu->vm->mm.first_table, vcpu->vm->vmid);
	restore_sysregs(&vcpu->cpu_sysregs);
}

// ハイパーバイザでの処理を終えて VM に処理を戻すときに kernel_exit から呼ばれる
void vm_entering_work() {
	struct vcpu_struct *vcpu = current_pcpu()->current_vcpu;

	if (!vcpu) {
		// todo: ハイパーバイザ(EL2)が動いているときに割込みが発生し、そのあと復帰すると起こる
		WARN("vCPU is NULL while entering to VM");
		return;
	}

	if (HAVE_FUNC(vcpu->vm->board_ops, entering_vm)) {
		vcpu->vm->board_ops->entering_vm(vcpu);
	}

	// VM 処理に復帰するとき、コンソールがこの VM に紐づいていたら
	// キューに入っていた値を全部出力する
	if (is_uart_forwarded_vm(vcpu->vm)) {
		flush_vm_console(vcpu->vm);
	}

	// todo: entering_vm, flush, set_cpu_sysregs, set_cpu_virtual_interrupt の正しい呼び出し順がわからない
	// 控えておいたレジスタの値を戻す
	set_cpu_sysregs(vcpu);

	// 今実行を再開しようとしている VM に対し仮想割込みを設定する
	//   ハイパーバイザ環境では VM に対し割込みを発生させる必要があるので
	//   VM が実行開始するタイミングで仮想割込みを生成しないといけない
	set_cpu_virtual_interrupt(vcpu);
}

// VM での処理を抜けてハイパーバイザに処理に入るときに kernel_entry から呼ばれる
void vm_leaving_work() {
	struct vcpu_struct *vcpu = current_pcpu()->current_vcpu;

    if (!vcpu) {
		// todo: ハイパーバイザ(EL2)が動いているときに割込みが発生すると起こる
		WARN("vCPU is NULL while leaving from VM");
        return;
    }

	// 今のレジスタの値を控える
	save_sysregs(&vcpu->cpu_sysregs);

	if (HAVE_FUNC(vcpu->vm->board_ops, leaving_vm)) {
		vcpu->vm->board_ops->leaving_vm(vcpu);
	}

	if (is_uart_forwarded_vm(vcpu->vm)) {
		flush_vm_console(vcpu->vm);
	}
}

static const char *vm_state_str[] = {
	"RUNNING",
	"RUNNABLE",
	"ZOMBIE",
};

// vCPU を実行している pCPU のインデックス(id)を返す
static int find_pcpu_which_runs(struct vcpu_struct *vcpu) {
	for (int i = 0; i < NUMBER_OF_PCPUS; i++) {
		if (pcpu_of(i)->current_vcpu == vcpu) {
			return i;
		}
	}
	return -1;
}

// `start_index` 以降の vCPU で、`vm` を実行している vCPU のインデックス(id)を返す
static int find_vcpu_which_runs(struct vm_struct2 *vm, int start_index) {
	for (; start_index < current_number_of_vcpus; start_index++) {
		if (vcpus[start_index]->vm == vm) {
			return start_index;
		}
	}
	return -1;
}

// todo: 各種 stat は vCPU ごとに計測したほうがいいかもしれない
static void show_vcpu_list(struct vm_struct2 *vm) {
	// todo: vcpu の index ではなく vm 内の cpuid を表示するべき
	for (int vcpu_idx = 0; (vcpu_idx = find_vcpu_which_runs(vm, vcpu_idx)) >= 0; vcpu_idx++) {
		struct vcpu_struct *vcpu = vcpus[vcpu_idx];
		int pcpu_idx = find_pcpu_which_runs(vcpu);
        if (pcpu_idx >= 0) {
            printf("%c %4s %12s %4d %4d 0x%08x %8s\n",
                /* %c   */ ' ',
                /* %4s  */ "",
                /* %12s */ "",
                /* %4d  */ vcpu->vcpu_id,
                /* %4d  */ pcpu_idx,
                /* %8x  */ vcpu_pt_regs(vcpu)->pc,
                /* %8s  */ vm_state_str[vcpu->state]);
        }
        else {
            printf("%c %4s %12s %4d    %c 0x%08x %8s\n",
                /* %c   */ ' ',
                /* %4s  */ "",
                /* %12s */ "",
                /* %4d  */ vcpu->vcpu_id,
                /* %4d  */ '-',
                /* %8x  */ vcpu_pt_regs(vcpu)->pc,
                /* %8s  */ vm_state_str[vcpu->state]);
        }
    }
}

void show_vm_list() {
    printf("%c %4s %12s %4s %4s %10s %8s %7s %7s %7s %7s %7s %7s\n",
		   /* %c   */ ' ',
		   /* %4s  */ "VMID",
		   /* %12s */ "Name",
		   /* %4s  */ "vCPU",
           /* %4s  */ "pCPU",
		   /* %10s */ "Saved-PC",
		   /* %8s  */ "State",
		   /* %7s  */ "Pages",
		   /* %7s  */ "WFX",
		   /* %7s  */ "HVC",
		   /* %7s  */ "SysRegs",
		   /* %7s  */ "PF",
		   /* %7s  */ "MMIO");
	for (int i = 0; i < current_number_of_vms; i++) {
        struct vm_struct2 *vm = vms2[i];
        printf("%c %4d %12s %4s %4s %10s %8s %7d %7d %7d %7d %7d %7d\n",
               /* %c   */ is_uart_forwarded_vm(vm) ? '*' : ' ',
			   /* %4d  */ vm->vmid,
			   // todo: vm->name を使いたい
			   /* %12s */ vm->name ? vm->loader_args.filename : "",
			   /* %4s  */ "",
			   /* %4s  */ "",
			   /* %10s */ "",
			   /* %8s  */ "",
			   /* %7d  */ vm->mm.vm_pages_count,
               /* %7d  */ vm->stat.wfx_trap_count,
			   /* %7d  */ vm->stat.hvc_trap_count,
               /* %7d  */ vm->stat.sysregs_trap_count,
			   /* %7d  */ vm->stat.pf_trap_count,
               /* %7d  */ vm->stat.mmio_trap_count);
		show_vcpu_list(vm);
    }
}

// EL2 から EL1 に遷移し、VM を復帰させる
// vCPU から vCPU の遷移ではなく、必ず scheduler から vCPU への遷移
static void schedule(struct vcpu_struct *vcpu) {
	struct pcpu_struct *pcpu = current_pcpu();

    // vCPU を実行するので、ステータスを更新
	vcpu->state = VCPU_RUNNING;     // この vCPU は今実行中
	pcpu->current_vcpu = vcpu;      // この pCPU はこの vCPU を実行中

    // todo: sysregs に控えてあるから明示的なセットは不要かもしれない
    // 仮想 CPU ID をセット
    // 本来は MT ビットを見て Aff0 がスレッド ID か CPU ID かを判断しなくてはいけないが
    // get_cpuid も含めて Aff0 が CPU ID であることを前提にしているので、ここでは無視する
    // set_vmpidr_el2(0x80000000 | vcpu->vcpu_id);

	DEBUG("Schedule from hv: vcpu=%d(0x%lx), lock=%d, pcpu=%d", vcpu->vcpu_id, vcpu, vcpu->lock.locked, pcpu->id);
	// vcpu を実行する(しばらくここには帰ってこない)
	cpu_switch_to(&pcpu->scheduler_context, vcpu);
    // ここに帰ってきたということは、おそらく yield されて scheduler に戻ってきた
	DEBUG("Return to hv: vcpu=%d(0x%lx), lock=%d, pcpu=%d", vcpu->vcpu_id, vcpu, vcpu->lock.locked, pcpu->id);

	// 復帰後に vcpu が別の pcpu で実行されるかもしれないので、pcpu を再取得する
	pcpu = current_pcpu();

	// vCPU を停止するので、ステータスを更新
	vcpu->state = (vcpu->state == VCPU_ZOMBIE) ?    // この vCPU は、実行可能か、終了済み
                    VCPU_ZOMBIE : VCPU_RUNNABLE;
	pcpu->current_vcpu = &pcpu->scheduler_context;  // この pCPU は HV(スケジューラ)を実行している
}

// todo: タイマで yield を呼び出すと、次回復帰時にそこから再開してしまうのでは
//       特に問題ない？
// VM が CPU 時間を手放しハイパーバイザに切り替える
void yield() {
	struct pcpu_struct *pcpu = current_pcpu();
	struct vcpu_struct *vcpu = pcpu->current_vcpu;

    // ハイパーバイザ実行中に yield される可能性はある
    if (!vcpu) {
        // todo: この場合、たとえばタイマ発火は無視されたことになる
        INFO("Yield while EL2");
        return;
    }

	// ロックを取ってから idle_vm に切り替える
	acquire_lock(&vcpu->lock);

    // vCPU を停止するので、ステータスを更新
	// 割込みの有効・無効状態は CPU の状態ではなくこのスレッドの状態なので、退避・復帰させる必要がある
	//vcpu->state = VCPU_RUNNABLE;    					// この vCPU は、実行可能か、終了済み
    //pcpu->current_vcpu = &pcpu->scheduler_context;    // この pCPU は HV(スケジューラ)を実行している
    // int interrupt_enable = vcpu->interrupt_enable;

	DEBUG("Yield to hv: vcpu=%d(0x%lx), lock=%d, pcpu=%d", vcpu->vcpu_id, vcpu, vcpu->lock.locked, pcpu->id);

	// スケジューラに復帰する(しばらくここには帰ってこない)
	cpu_switch_to(vcpu, &pcpu->scheduler_context);
    // ここに帰ってきたということは、おそらく schedule されて vCPU に戻ってきた

	DEBUG("Return from hv to yield: vcpu=%d(0x%lx), lock=%d, pcpu=%d", vcpu->vcpu_id, vcpu, vcpu->lock.locked, pcpu->id);

	// 復帰後に vcpu が別の pcpu で実行されるかもしれないので、pcpu を再取得する
	pcpu = current_pcpu();

    // vCPU を実行するので、ステータスを更新
	vcpu->state = VCPU_RUNNING;                 // この vCPU は今実行中
	pcpu->current_vcpu = vcpu;                  // この pCPU はこの vCPU を実行中
    // vcpu->interrupt_enable = interrupt_enable;  // 割込みの有効・無効状態を復帰させる

	// また戻ってきたらロックを解放する
	release_lock(&vcpu->lock);
}

// 各コア専用に用意された idle vcpu で実行され、タイマ割込みが発生するとここに帰ってくる
// 切り替える前に必ず VM のロックを取り、切り替え終わったらすぐにロックを解放する
// todo: 割込みを無効にしないといけないタイミングがありそう
// todo: hypervisor 実行中に割込みは処理してもいいが、コンテキストスイッチはしてはいけない
void scheduler(unsigned long cpuid) {
	struct vcpu_struct *vcpu;
	int found;

	// todo: 割込みを有効にした直後に、既に発生していた割込みが発生する
	//       その結果 vm_exit が実行されるが、まだ vm を実行していないので warn となっている
	//       先に idle vcpu に切り替えてから割込みを有効にするという方法を取る？
	// この CPU コアの割込みを有効化
	// enable_irq();

	// 最初に idle vcpu に切り替え
	vcpu = vcpus[cpuid];
	acquire_lock(&vcpu->lock);
	vcpu->interrupt_enable = 1;
	vcpu->number_of_off = 1;
	schedule(vcpu);
	release_lock(&vcpu->lock);

    // todo: 割込みをどうするか考える、ただしタスクスイッチは禁止しないといけない
	//       scheduler 実行中は割込み禁止でいいのでは
	while (1) {
		found = 0;

		// 単純なラウンドロビンで vCPU に CPU 時間を割り当てる
		// 先頭の vCPU は idle vCPU なので飛ばす
		for (int i = NUMBER_OF_PCPUS; i < NUMBER_OF_VCPUS; i++) {
			vcpu = vcpus[i];

			// vCPU が無効な場合はスキップ
			if (!vcpu) {
				continue;
			}

			acquire_lock(&vcpu->lock);

			// RUNNABLE 状態の vCPU を探す
			if (vcpu && vcpu->state == VCPU_RUNNABLE) {
				found = 1;
				schedule(vcpu);
			}

			release_lock(&vcpu->lock);
		}

		// 全 vCPU を走査しても実行できる vCPU がひとつも見つからなかったら IDLE vCPU を実行
		if (!found) {
			vcpu = vcpus[cpuid];
			acquire_lock(&vcpu->lock);
			schedule(vcpu);
			release_lock(&vcpu->lock);
		}
	}
}
