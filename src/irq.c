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

// メインコアの割込みハンドラは Mailbox 以外の割込みを処理する
static void handle_irq_maincore() {
	// todo: daifset で割込みを止めてもシステムタイマによる割込みが発生してしまう、なぜ？
	unsigned long basic_pending = get32(IRQ_BASIC_PENDING);

	if (basic_pending & PENDING_REGISTER_1_BIT) {
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

// サブコアの割込みハンドラは Mailbox の割込みのみを処理する
static void handle_irq_subcore(unsigned long cpuid) {
	static unsigned int mbox_sources[] = {
		CORE1_IRQ_SOURCE, CORE2_IRQ_SOURCE, CORE3_IRQ_SOURCE
	};
	static unsigned int mbox_rd_clrs[] = {
		MBOX_CORE1_RD_CLR_0, MBOX_CORE2_RD_CLR_0, MBOX_CORE3_RD_CLR_0
	};

	unsigned long source = get32(mbox_sources[cpuid - 1]);

	if (source & IRQ_SOURCE_MBOX_0_BIT) {
		// INFO("source: 0x%lx", source);

		// todo: main で1回だけ mailbox に書くとずっと割込み発生する
		//       ここでクリアすれば止まる
		//       ということは、mailbox の割込みはうまく発生しているが、
		//       割込みハンドラ内で割込みのフラグがうまく読めていない
		//       期待値は basic_irq の mailbox のビットが立つことだが、そうなってない
		// -> source を読めば判断はできる
		// source の各ビットを独立して使用可能
		put32(mbox_rd_clrs[cpuid - 1], 0x1);

		handle_mailbox_irq(cpuid);
	}
}

// 割込みベクタからジャンプしてくる先
// RPi3B では外部割込みはどれかひとつのコアでしか受信できない(デフォルトはコア0)
//   https://github.com/s-matyukevich/raspberry-pi-os/blob/master/docs/lesson03/linux/interrupt_controllers.md
//   "by default local interrupt controller is configured in such a way that all external interrupts are sent to the first core"
void handle_irq(void)
{
	// PENDING レジスタは全コア共通なので、CPU ID を見てコアごとに処理する割込みを分ける必要がある
	// たとえばシステムタイマ割込みはコア0が処理する前提になっているが、
	// Mailbox 割込みとタイミングがぶつかると、コア1で処理されてしまう可能性がある
	unsigned long cpuid = get_cpuid();
	if (cpuid == 0) {
		handle_irq_maincore();
	} else {
		handle_irq_subcore(cpuid);
	}
}
