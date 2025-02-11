#ifndef _SYNC_EXC_H
#define _SYNC_EXC_H

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
#define ESR_EL2_EC_IABT_LOW		32	// Instruction Abort from a lower Exception level
#define ESR_EL2_EC_DABT_LOW		36	// Data Abort from a lower Exception level

#endif /* _SYNC_EXC_H */