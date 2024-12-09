#include "sync_exc.h"
#include "arm/sysregs.h"
#include "printf.h"

// ***************************************
// ESR_EL2, Exception Syndrome Register (EL2)
// ***************************************

// ESR_EL2 の中の EC フィールドへのインデックス
#define ESR_EL2_EC_SHIFT		26

// EC(exception class)の種類
// https://developer.arm.com/documentation/ddi0595/2021-03/AArch64-Registers/ESR-EL2--Exception-Syndrome-Register--EL2-?lang=en#fieldset_0-31_26
#define ESR_EL2_EC_TRAP_WFX     1	// WF* instruction execution
#define ESR_EL2_EC_TRAP_FP_REG	7	// Access to SVE, Advanced SIMD or floating-point functionality
#define ESR_EL2_EC_HVC64        22	// HVC instruction execution when HVC is not disabled
#define ESR_EL2_EC_TRAP_SYSTEM	24	// MSR, MRS or System instruction exection that is not reported using EC 
#define ESR_EL2_EC_TRAP_SVE		25	// Access to SVE functionality
#define ESR_EL2_EC_DABT_LOW		36	// Data Abort from a lower Exception level

// eclass のインデックスに合わせたエラーメッセージ
static const char *sync_error_reasons[] = {
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

void handle_sync_exception(unsigned long esr, unsigned long elr, unsigned long far, unsigned long hvc_nr) {
	int eclass = (esr >> ESR_EL2_EC_SHIFT) & 0x3f;
	switch (eclass)
	{
	case ESR_EL2_EC_TRAP_WFX:
		printf("TRAP_WFX\n");
		break;
	case ESR_EL2_EC_TRAP_FP_REG:
		INFO("TRAP_FP_REG");
		break;
	case ESR_EL2_EC_HVC64:
		INFO("HVC(%d)!", hvc_nr);
		break;
	case ESR_EL2_EC_TRAP_SYSTEM:
		INFO("TRAP_SYSTEM");
		break;
	case ESR_EL2_EC_TRAP_SVE:
		INFO("TRAP_SVE");
		break;
	case ESR_EL2_EC_DABT_LOW:
		printf("DABT_LOW");
		break;
	default:
		PANIC("uncaught synchronous exception:\n%s\nesr: %x, address: %x", sync_error_reasons[eclass], esr, elr);
		break;
	}
}
