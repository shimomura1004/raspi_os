#include "utils.h"
#include "timer.h"
#include "entry.h"
#include "peripherals/irq.h"
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
}

void show_invalid_entry_message(int type, unsigned long esr, unsigned long elr, unsigned long far, unsigned long mpidr)
{
	PANIC("uncaught exception(%s) esr: 0x%lx, elr: 0x%lx, far: 0x%lx, mpidr: 0x%lx",
		  entry_error_messages[type], esr, elr, far, mpidr);
}

// 割込みベクタがジャンプしてくる先
void handle_irq(void)
{
	unsigned int irq = get32(IRQ_PENDING_1);
	if (irq & SYSTEM_TIMER_IRQ_1_BIT) {
		irq &= ~SYSTEM_TIMER_IRQ_1_BIT;
		handle_timer1_irq();
	}
	if (irq & SYSTEM_TIMER_IRQ_3_BIT) {
		irq &= ~SYSTEM_TIMER_IRQ_3_BIT;
		handle_timer3_irq();
	}
	if (irq & AUX_IRQ_BIT) {
		irq &= ~AUX_IRQ_BIT;
		handle_uart_irq();
	}

	if (irq) {
		WARN("unknown pending irq: %x", irq);
	}
}
