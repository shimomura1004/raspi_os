#include "utils.h"
#include "printf.h"
#include "timer.h"
#include "entry.h"
#include "peripherals/irq.h"
#include "arm/sysregs.h"
#include "sched.h"

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
// この関数では全割込みのうちタイマ1だけを有効化する
//   ちなみにタイマは4個あるが 0 と 2 は GPU で使われる
void enable_interrupt_controller()
{
	put32(ENABLE_IRQS_1, SYSTEM_TIMER_IRQ_1);
}

// eclass のインデックスに合わせたエラーメッセージ
const char *sync_error_reasons[] = {
	"Unknown reason.",
	"Trapped WFI or WFE instruction execution.",
	"(unknown)",
	"Trapped MCR or MRC access with (coproc==0b1111).",
	"Trapped MCRR or MRRC access with (coproc=0b1111).",
	"Trapped MCR or MRC access with (coproc==0b1110).",
	"Trapped LDC or STC access.",
	"Access to SVE, Advanced SIMD, or floating-point functionality trapped by CPACR_EL1.FPEN, CPTR_EL2.FPEN, CPTR_EL2.TFP, or CPTR_EL3.TFP control.",
	"Trapped VMRS access, from ID group trap.",
	"Trapped use of a Pointer authentication instruction because HCR_EL2.API == 0 || SCR_EL3.API == 0.",
	"(unknown)",
	"(unknown)",
	"Trapped MRRC access with (coproc==0b1110).",
	"Branch Target Exception.",
	"Ilegal Execution state.",
	"(unknown)",
	"(unknown)",
	"SVC instruction execution in AArch32 state.",
	"HVC instruction execution in AArch32 state.",
	"SMC instruction execution in AArch32 state.",
	"(unknown)",
	"SVC instruction execution in AArch64 state.",
	"HVC instruction execution in AArch64 state.",
	"SMC instruction execution in AArch64 state.",
	"Trapped MSR, MRS or System instruction execution in AArch64 state.",
	"Access to SVE functionality trapped as a result of CPAR_EL1.ZEN, CPTR_EL2.ZEN, CPTR_EL.TZ, or CPTR_EL3.EZ.",
	"Trapped ERET, ERETAA, or ERETAB instruction execution.",
	"(unknown)",
	"Exception from a Pointer Authentication instruction authentication failure.",
	"(unknown)",
	"(unknown)",
	"(unknown)",
	"Instruction Abort from a lower Exception level.",
	"Instruction Abort taken without a change in Exception level.",
	"PC alignment fault exception.",
	"(unknown)",
	"Data Abort from a lower Exception level.",
	"Data Abort without a change in Exception level, or Data Aborts taken to EL2 as a result of access generated associated with VNCR_EL2 as part of nested virtualization support.",
	"SP alignment fault exception.",
	"(unknown)",
	"Trapped floating-point exception taken from AArch32 state.",
	"(unknown)",
	"(unknown)",
	"(unknown)",
	"Trapped floating-point exception taken from AArch64 state.",
	"(unknown)",
	"(unknown)",
	"SError interrupt.",
	"Breakpoint exception from a lower Exception level.",
	"Breakpoint exception taken without a change in Exception level.",
	"Software Step exception from a lower Exception level.",
	"Software Step exception taken without a change in Exception level.",
	"Watchpoint from a lower Exception level.",
	"Watchpoint exceptions without a change in Exception level, or Watchpoint exceptions taken to EL2 as a result of accesses generated associated with VNCR_EL2 as part of nested virtualization support.",
	"(unknown)",
	"(unknown)",
	"BKPT instruction execution in AArch32 state.",
	"(unknown)",
	"Vector Catch exception from AArch32 state.",
	"(unknown)",
	"BRK instruction execution in AArch64 state.",
};

void show_invalid_entry_message(int type, unsigned long esr, unsigned long address)
{
	printf("%s, ESR: %x, address: %x\r\n", entry_error_messages[type], esr, address);
}

// ハンドルできない同期例外が発生した場合の専用のログプリンタ
void show_uncaught_sync_exception_message(int type, unsigned long esr, unsigned long address) {
	int eclass = (esr >> ESR_EL2_EC_SHIFT) & 0x3f;
	struct cpu_sysregs sysregs;
	printf("%s, ESR: %x, address: %x\nreason: %s\n",
		entry_error_messages[type], esr,
		address, sync_error_reasons[eclass]);
	_get_sysregs(&sysregs);
	unsigned long _get_sctlr_el1(void);
	unsigned long _get_spsr_el2(void);
	printf("SCTLR_EL1 = %x, SPSR_EL2 = %x\n", _get_sctlr_el1(), _get_spsr_el2());
}

// 割込みベクタがジャンプしてくる先
void handle_irq(void)
{
	unsigned int irq = get32(IRQ_PENDING_1);
	switch (irq) {
		case (SYSTEM_TIMER_IRQ_1):
			handle_timer_irq();
			break;
		default:
			printf("Unknown pending irq: %x\r\n", irq);
	}
}
