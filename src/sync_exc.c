#include "sync_exc.h"
#include "mm.h"
#include "sched.h"
#include "debug.h"
#include "vm.h"
#include "arm/sysregs.h"

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

static void handle_trap_wfx() {
	schedule();
	increment_current_pc(4);
}

static void handle_trap_system(unsigned long esr) {
// ESR_EL2.ISS encding for an exception from MSR, MRS
// IL[25] instruction length for synchronous exceptions
//   0b0: 16-bit instruction trapped
//   0b1: 32-bit instruction trapped
// Op0[21:20]
// Op2[19:17]
// Op1[16:14]
// CRn[13:10]
// Rt[9:5]
// CRm[4:1]
// Direction[0]
//   0b0: Write access, including MSR instructions
//   0b1: Read access, including MRS instructions

#define DEFINE_SYSREG_MSR(name, _op1, _crn, _crm, _op2) \
	do { \
		if (op1 == (_op1) && crn == (_crn) && crm == (_crm) && op2 == (_op2)) { \
			current->cpu_sysregs.name = regs->regs[rt]; \
			goto sys_fin; \
		} \
	} while (0)

#define DEFINE_SYSREG_MRS(name, _op1, _crn, _crm, _op2) \
	do { \
		if (op1 == (_op1) && crn == (_crn) && crm == (_crm) && op2 == (_op2)) { \
			regs->regs[rt] = current->cpu_sysregs.name; \
			goto sys_fin; \
		} \
	} while (0)

	// hypercall した VM のプロセス情報を取り出す
	struct pt_regs *regs = vm_pt_regs(current);

	// ESR.ISS[24:0] instruction specific syndrome
	// exception class に応じて使われ方が違う
	// これは MSR/MRS のときのフォーマットを前提にしている
	unsigned int op0 = (esr >> 20) & 0x03;
	unsigned int op2 = (esr >> 17) & 0x07;
	unsigned int op1 = (esr >> 14) & 0x07;
	unsigned int crn = (esr >> 10) & 0x0f;
	unsigned int rt  = (esr >>  5) & 0x1f;
	unsigned int crm = (esr >>  1) & 0x0f;
	unsigned int dir = esr         & 0x01;

	// INFO("trap_system: op0=%d, op2=%d, op1=%d, crn=%d, rt=%d, crm=%d, dir=%d",
	// 	 op0, op2, op1, crn, rt, crm, dir);
	// INFO("id_aa64mmfr0_el1 = %d", current->cpu_sysregs.id_aa64mmfr0_el1);

	// todo: (op0 & 2) の意図が分からない 
	if ((op0 & 2) && dir == 0) {
		// msr
		// todo: マクロの引数に使われている定数の意味は？
		DEFINE_SYSREG_MSR(actlr_el1, 0, 1, 0, 1);
		DEFINE_SYSREG_MSR(csselr_el1, 1, 0, 0, 0);
	}
	else if ((op0 & 2) && dir == 1) {
		// mrs
		DEFINE_SYSREG_MRS(actlr_el1, 0, 1, 0, 1);
		DEFINE_SYSREG_MRS(id_pfr0_el1, 0, 0, 1, 0);
		DEFINE_SYSREG_MRS(id_pfr1_el1, 0, 0, 1, 1);
		DEFINE_SYSREG_MRS(id_mmfr0_el1, 0, 0, 1, 4);
		DEFINE_SYSREG_MRS(id_mmfr1_el1, 0, 0, 1, 5);
		DEFINE_SYSREG_MRS(id_mmfr2_el1, 0, 0, 1, 6);
		DEFINE_SYSREG_MRS(id_mmfr3_el1, 0, 0, 1, 7);
		DEFINE_SYSREG_MRS(id_isar0_el1, 0, 0, 2, 0);
		DEFINE_SYSREG_MRS(id_isar1_el1, 0, 0, 2, 1);
		DEFINE_SYSREG_MRS(id_isar2_el1, 0, 0, 2, 2);
		DEFINE_SYSREG_MRS(id_isar3_el1, 0, 0, 2, 3);
		DEFINE_SYSREG_MRS(id_isar4_el1, 0, 0, 2, 4);
		DEFINE_SYSREG_MRS(id_isar5_el1, 0, 0, 2, 5);
		DEFINE_SYSREG_MRS(mvfr0_el1, 0, 0, 3, 0);
		DEFINE_SYSREG_MRS(mvfr1_el1, 0, 0, 3, 1);
		DEFINE_SYSREG_MRS(mvfr2_el1, 0, 0, 3, 2);
		DEFINE_SYSREG_MRS(id_aa64pfr0_el1, 0, 0, 4, 0);
		DEFINE_SYSREG_MRS(id_aa64pfr1_el1, 0, 0, 4, 1);
		DEFINE_SYSREG_MRS(id_aa64dfr0_el1, 0, 0, 5, 0);
		DEFINE_SYSREG_MRS(id_aa64dfr1_el1, 0, 0, 5, 1);
		DEFINE_SYSREG_MRS(id_aa64isar0_el1, 0, 0, 6, 0);
		DEFINE_SYSREG_MRS(id_aa64isar1_el1, 0, 0, 6, 1);
		DEFINE_SYSREG_MRS(id_aa64mmfr0_el1, 0, 0, 7, 0);
		DEFINE_SYSREG_MRS(id_aa64mmfr1_el1, 0, 0, 7, 1);
		DEFINE_SYSREG_MRS(id_aa64afr0_el1, 0, 0, 5, 4);
		DEFINE_SYSREG_MRS(id_aa64afr1_el1, 0, 0, 5, 5);
		DEFINE_SYSREG_MRS(ctr_el0, 3, 0, 0, 1);
		DEFINE_SYSREG_MRS(ccsidr_el1, 1, 0, 0, 0);
		DEFINE_SYSREG_MRS(clidr_el1, 1, 0, 0, 1);
		DEFINE_SYSREG_MRS(csselr_el1, 2, 0, 0, 0);
		DEFINE_SYSREG_MRS(aidr_el1, 1, 0, 0, 7);
		DEFINE_SYSREG_MRS(revidr_el1, 0, 0, 0, 6);
	}
	// todo: else が必要では？
	WARN("system register access is not handled");
sys_fin:
	increment_current_pc(4);
	return;
}

// ESR_EL2
// https://developer.arm.com/documentation/ddi0595/2021-03/AArch64-Registers/ESR-EL2--Exception-Syndrome-Register--EL2-?lang=en#fieldset_0-24_0
void handle_sync_exception(unsigned long esr, unsigned long elr, unsigned long far) {
	// esr にはハンドラ el01_sync により esr_el2 が渡されている
	// esr_el2 の下位16ビットに hvc 実行時に指定した即値が入っている
	// 今の実装では hvc に渡された数値は無視している

	// EC(error class)を取得
	int eclass = (esr >> ESR_EL2_EC_SHIFT) & 0x3f;

	// HVC 例外は handle_sync_exception_hvc64 で処理するためここには現れない
	switch (eclass)
	{
	case ESR_EL2_EC_TRAP_WFX:
		current->stat.wfx_trap_count++;
		// ゲスト VM が WFI/WFE を実行したら VM を切り替える
		handle_trap_wfx();
		break;
	case ESR_EL2_EC_TRAP_FP_REG:
		WARN("TRAP_FP_REG is not implemented.");
		break;
	case ESR_EL2_EC_TRAP_SYSTEM:
		current->stat.sysregs_trap_count++;
		handle_trap_system(esr);
		break;
	case ESR_EL2_EC_TRAP_SVE:
		WARN("TRAP_SVE is not implemented.");
		break;
	case ESR_EL2_EC_IABT_LOW:
		WARN("IABT_LOW is not implemented.");
		WARN("%s\nesr: 0x%lx, address: 0x%lx", sync_error_reasons[eclass], esr, elr);
		break;
	case ESR_EL2_EC_DABT_LOW:
		// todo: ゲストが uart の状態を読むたびに vmexit/enter が発生している
		// データアボートは処理し、問題なければそのまま復帰
		if (handle_mem_abort(far, esr) < 0) {
			PANIC("handle_mem_abort() failed.");
		}
		break;
	default:
		PANIC("uncaught synchronous exception:\n%s\nesr: 0x%lx, address: 0x%lx", sync_error_reasons[eclass], esr, elr);
		break;
	}
}

// EL1 からのハイパーコールの処理
// todo: hvc_nr をマジックナンバと比較しない
void handle_sync_exception_hvc64(unsigned long hvc_nr, unsigned long a0, unsigned long a1, unsigned long a2, unsigned long a3) {
	current->stat.hvc_trap_count++;

	switch (hvc_nr) {
	case 0:
		WARN("HVC #%lu", hvc_nr);
		break;
	case 1:		// 第1引数の非負整数をプリント
		INFO("HVC #%d: 0x%lx(%ld)", hvc_nr, a0, a0);
		break;
	case 2:		// 第1,2引数の非負整数をプリント
		INFO("HVC #%d: 0x%lx(%ld), 0x%lx(%ld)", hvc_nr, a0, a0, a1, a1);
		break;
	case 3:		// 第1,2,3引数の非負整数をプリント
		INFO("HVC #%d: 0x%lx(%ld), 0x%lx(%ld), 0x%lx(%ld)", hvc_nr, a0, a0, a1, a1, a2, a2);
		break;
	case 4:		// 第1,2,3,4引数の非負整数をプリント
		INFO("HVC #%d: 0x%lx(%ld), 0x%lx(%ld), 0x%lx(%ld), 0x%lx(%ld)", hvc_nr, a0, a0, a1, a1, a2, a2, a3, a3);
		break;
	default:
		WARN("uncaught hvc64 exception: %ld", hvc_nr);
		break;
	}
}
