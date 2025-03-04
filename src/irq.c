#include "utils.h"
#include "systimer.h"
#include "entry.h"
#include "peripherals/irq.h"
#include "peripherals/mailbox.h"
#include "arm/sysregs.h"
#include "sched.h"
#include "debug.h"
#include "mini_uart.h"

const char *entry_error_messages[] = {
	"SYNC_INVALID_EL2",
	"IRQ_INVALID_EL2",		
	"FIQ_INVALID_EL2",		
	"ERROR_INVALID_EL2",		

	"SYNC_INVALID_EL01_64",		
	"IRQ_INVALID_EL01_64",		
	"FIQ_INVALID_EL01_64",		
	"ERROR_INVALID_EL01_64",		

	"SYNC_INVALID_EL01_32",		
	"IRQ_INVALID_EL01_32",		
	"FIQ_INVALID_EL01_32",		
	"ERROR_INVALID_EL01_32",	

	"SYNC_ERROR",
	"HVC_ERROR",
	"DATA_ABORT_ERROR",
};

// RPi は割込みの有効・無効の管理用に3つのレジスタを持つ
//   #define ENABLE_IRQS_1		(PBASE+0x0000B210)
//   #define ENABLE_IRQS_2		(PBASE+0x0000B214)
//   #define ENABLE_BASIC_IRQS	(PBASE+0x0000B218)
//   BASIC IRQS はローカル割込み用
// この関数では全割込みのうちタイマ1,3と UART を有効化する
//   ちなみにタイマは4個あるが 0 と 2 は GPU で使われる
void enable_interrupt_controller()
{
	put32(ENABLE_IRQS_1, SYSTEM_TIMER_IRQ_1_BIT);
	put32(ENABLE_IRQS_1, SYSTEM_TIMER_IRQ_3_BIT);
	put32(ENABLE_IRQS_1, AUX_IRQ_BIT);

	// Mailbox 割込みを有効化
	put32(ENABLE_BASIC_IRQS, MBOX_IRQ_BIT);
}

void show_invalid_entry_message(int type, unsigned long esr, unsigned long elr, unsigned long far, unsigned long mpidr)
{
	PANIC("uncaught exception(%s) esr: 0x%lx, elr: 0x%lx, far: 0x%lx, mpidr: 0x%lx",
		  entry_error_messages[type], esr, elr, far, mpidr);
}

// 割込みベクタがジャンプしてくる先
// todo: コアごとにハンドラをわける
void handle_irq(void)
{
	unsigned long cpuid = get_cpuid();

	// todo: daifset で割込みを止めてもシステムタイマによる割込みが発生してしまう、なぜ？
	// todo: cpu1 で実行すると uart の割込みが発生しない、なぜ？
	//   https://github.com/s-matyukevich/raspberry-pi-os/blob/master/docs/lesson03/linux/interrupt_controllers.md
	//   "by default local interrupt controller is configured in such a way that all external interrupts are sent to the first core"

	// todo: なぜ irq_pending_1 を直接読んでいる？まず basic_pending を読むべきでは？
	//       → 修正した
	// todo: この get32 が、el2 で呼び出したときもトラップされている可能性がある？
	//       main で enable_irq した直後に el01_irq が呼ばれているが、これはなにか…？
	// PENDING レジスタは全コア共通なので、CPU ID を見てコアごとに処理する割込みを分ける必要がある
	// たとえばシステムタイマ割込みはコア0が処理する前提になっているが、
	// Mailbox 割込みとタイミングがぶつかると、コア1で処理されてしまう可能性がある
	unsigned int basic_pending = get32(IRQ_BASIC_PENDING);
	if (cpuid != 0) {
		unsigned long source = get32(CORE1_IRQ_SOURCE);
		INFO("basic_pending: 0x%lx, source: 0x%lx", basic_pending, source);


		// todo: main で1回だけ mailbox に書くとずっと割込み発生する
		//       ここでクリアすれば止まる
		//       ということは、mailbox の割込みはうまく発生しているが、
		//       割込みハンドラ内で割込みのフラグがうまく読めていない
		//       期待値は basic_irq の mailbox のビットが立つことだが、そうなってない
		put32(MBOX_CORE1_RD_CLR_0, 1);
	}
	if (cpuid != 0 && (basic_pending & MBOX_IRQ_BIT)) {
		// todo: basic_pending を直接変更することはできない
		//       mbox 自体の割込み状態をクリアする必要あり
		//basic_pending &= ~MBOX_IRQ_BIT;
		// handle_mailbox_irq();
		PANIC("MAILBOX!");
	}
	if (cpuid == 0 && (basic_pending & PENDING_REGISTER_1_BIT)) {
		unsigned int irq = get32(IRQ_PENDING_1);
		if (irq & SYSTEM_TIMER_IRQ_1_BIT) {
			irq &= ~SYSTEM_TIMER_IRQ_1_BIT;
			handle_systimer1_irq();
		}
		if (irq & SYSTEM_TIMER_IRQ_3_BIT) {
			irq &= ~SYSTEM_TIMER_IRQ_3_BIT;
			handle_systimer3_irq();
		}
		if (irq & AUX_IRQ_BIT) {
			irq &= ~AUX_IRQ_BIT;
			handle_uart_irq();
		}
		if (irq) {
			WARN("unknown pending irq: %x", irq);
		}
	}
	if (basic_pending & PENDING_REGISTER_2_BIT) {
		unsigned int irq = get32(IRQ_PENDING_2);
		if (irq) {
			WARN("unknown pending irq: %x", irq);
		}
	}
}
