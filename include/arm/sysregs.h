#ifndef _SYSREGS_H
#define _SYSREGS_H

// ***************************************
// SCTLR_EL2, System Control Register (EL2), Page 2025 of AArch64-Reference-Manual.
// ***************************************

// SCTLR: sytem control register
// EE[25]:
//   0b0: little-endian
//   0b1: big-endian
// I[12] (instruction access cacheability control):
//   0b0: 命令キャッシュ不可
//   0b1: 命令キャッシュ可
// C[2] (cacheability control):
//   0b0: データキャッシュ不可
//   0b1: データキャッシュ可
// M[0] (MMU enable):
//   0b0: 無効
//   0b1: 有効
#define SCTLR_EE                    (0 << 25)
#define SCTLR_I_CACHE_DISABLED      (0 << 12)
#define SCTLR_D_CACHE_DISABLED      (0 << 2)
#define SCTLR_MMU_DISABLED          (0 << 0)
#define SCTLR_MMU_ENABLED           (1 << 0)

#define SCTLR_VALUE_MMU_DISABLED \
	(SCTLR_EE | SCTLR_I_CACHE_DISABLED | SCTLR_D_CACHE_DISABLED | SCTLR_MMU_DISABLED)

// ***************************************
// HCR_EL2, Hypervisor Configuration Register (EL2), Page 1923 of AArch64-Reference-Manual.
// ***************************************

// HCR: hypervisor configuration register
// 一部のレジスタアクセスをトラップするよう設定するためのビット
//
// TID5[58] Trap ID group 5
//   GMID_EL1 へのアクセスを EL2 でトラップする
//   0b0: This control does not cause any instructions to be trapped
//   0b1: The specified EL1 assesses to ID group 5 registers are trapped to EL2
// EnSCXT[53] Enable access to the SCXTNUM_EL1 and SCXTNUM_EL0 registers
//   0b0: EL1 accesses to SCXTNUM_EL0 and SCXTNUM_EL1 are disabled,
//        causing an exception to EL2
//   0b1: This control does not cause accesses to SCXTNUM_EL0 and SCXTNUM_EL1 to be trapped
// TID4[49] Trap ID group 4
//   以下のレジスタアクセスを EL2 にトラップする
//     EL1 reads of CCSIDR_EL1, CCSIDR2_EL1, CLIDR_EL1, and CSSELR_EL1.
//     EL1 writes to CSSELR_EL1.
//   0b0: This control does not cause any instructions to be trapped.
//   0b1: The specified EL1 accesses to ID group 4 registers are trapped to EL2.
// FIEN[47] Fault Injection Enable
//   以下のレジスタアクセスを EL2 にトラップする
//     ERXPFGCDN_EL1, ERXPFGCTL_EL1, and ERXPFGF_EL1
//   0b0: Accesses to the specified registers from EL1 are trapped to EL2,
//        when EL2 is enabled in the current Security state.
//   0b1: This control does not cause any instructions to be trapped.
// TERR[36] Trap accesses of Error Record registers
//     MRS and MSR accesses to ERRSELR_EL1, ERXADDR_EL1, ERXCTLR_EL1,
//     ERXMISC0_EL1, ERXMISC1_EL1, and ERXSTATUS_EL1.
//     MRS accesses to ERRIDR_EL1 and ERXFR_EL1.
//   0b0: Accesses of the specified Error Record registers are not trapped by this mechanism.
//   0b1: Accesses of the specified Error Record registers at EL1 are trapped to EL2,
//        unless the instruction generates a higher priority exception.
// TLOR[35] Trap LOR registers
//   Traps Non-secure EL1 accesses to LORSA_EL1, LOREA_EL1,
//   LORN_EL1, LORC_EL1, and LORID_EL1 registers to EL2.
//     LORegions: limited ordering regions
//   0b0: This control does not cause any instructions to be trapped.
//   0b1: Non-secure EL1 accesses to the LOR registers are trapped to EL2.
// TRVM[30] Trap Reads of Virtual Memory controls.
//   Traps reads of the virtual memory control registers to EL2.
//   SCTLR_EL1, TTBR0_EL1, TTBR1_EL1, TCR_EL1, ESR_EL1, FAR_EL1, AFSR0_EL1,
//   AFSR1_EL1, MAIR_EL1, AMAIR_EL1, and CONTEXTIDR_EL1.
// TDZ[28] Trap DC ZVA instructions
//   DC ZVA(Data Cache Zero by VA): 指定された領域のキャッシュをゼロクリアする
//   todo: なぜこの命令をトラップする必要がある？
// TVM[26] Trap Virtual memory controls.
//   Traps writes to the virtual memory control registers to EL2.
//   SCTLR_EL1, TTBR0_EL1, TTBR1_EL1, TCR_EL1, ESR_EL1, FAR_EL1,
//   AFSR0_EL1, AFSR1_EL1, MAIR_EL1, AMAIR_EL1, and CONTEXTIDR_EL1.
// VSE[8] Virtual SError exception
//   0b0: This mechanism is not making a virtual SError exception pending
//   0b1: A virtual SError exception is pending because of this mechanism
// VI[7] Virtual IRQ Interrupt
//   0b0: This mechanism is not making a virtual IRQ pending
//   0b1: A virtual IRQ is pending because of this mechanism
// VF[6] Virtual FIQ Interrupt
//   0b0: This mechanism is not making a virtual FIQ pending
//   0b1: A virtual FIQ is pending because of this mechanism

// #define HCR_TID5        (1 << 58)
// #define HCR_ENSCXT      (0 << 53)
// #define HCR_TID4        (1 << 49)
// #define HCR_FIEN        (0 << 47)
// #define HCR_TERR        (1 << 36)
// #define HCR_TLOR        (1 << 35)
// #define HCR_TRVM        (0 << 30)
// #define HCR_TDZ         (1 << 28)
// #define HCR_TVM         (1 << 26)

// TACR[21] Trap Auxiliary Control Registers.
//   Traps EL1 accesses to the Auxiliary Control Registers to EL2
//     0b0: This control does not cause any instructions to be trapped.
//     0b1: EL1 accesses to the specified registers are trapped to EL2
// TID3[18], TID2[17], TID1[16]
//   Trap ID group 1,2,3. Traps EL1 reads of group 1,2,3 ID registers to EL2
// TWE[14] Traps EL0 and EL1 execution of WFE instructions to EL2
// TWI[13] Traps EL0 and EL1 execution of WFI instructions to EL2
//   0b0: トラップしない
//   0b1: EL0 か EL1 で WFI 命令を実行すると EL2 でトラップされる
#define HCR_TACR        (1 << 21)
#define HCR_TID3        (1 << 18)
#define HCR_TID2        (1 << 17)
#define HCR_TID1        (1 << 16)
#define HCR_TWE         (1 << 14)
#define HCR_TWI         (1 << 13)

// HCR: hypervisor configuration register
// https://developer.arm.com/documentation/ddi0601/2024-12/AArch64-Registers/HCR-EL2--Hypervisor-Configuration-Register
// E2H[34] (EL2 host): ホスト OS が EL2 で動くかどうか、0なら無効、1なら有効
// RW[31] (exection state control for lower execption level):
//   0b0: lower level はすべて aarch32
//   0b1: EL1 の exception stte は aarch64
// TGE[27] (trap general exception from el0)
//   0b0: 特に意味なし
//   0b1: EL1 にきたすべての exception は EL2 に渡される
//        すべての仮想割込みは無効化される
//        割込み処理後、EL1 に戻ることを禁止する
//        など
// AMO[5] (physical serror exception routing)
//   0b0: 特に効果なし
//   0b1: physical serror 例外は EL2 で処理される、など
// IMO[4] (physiccal irq routing)
//   0b0: When executing at Exception levels below EL2, and EL2 is enabled in the current Security state:
//          When the value of HCR_EL2.TGE is 0, Physical IRQ interrupts are not taken to EL2.
//          When the value of HCR_EL2.TGE is 1, Physical IRQ interrupts are taken to EL2 unless they are routed to EL3.
//          Virtual IRQ interrupts are disabled.
//   0b1: When executing at any Exception level, and EL2 is enabled in the current Security state:
//          Physical IRQ interrupts are taken to EL2, unless they are routed to EL3.
//          When the value of HCR_EL2.TGE is 0, then Virtual IRQ interrupts are enabled.
// FMO[3] (physical FIQ routing)
//   0b0: HCR_EL2.TGE の値によって意味が変わる
//   0b1: physical FIQ interrupt は EL2 で処理される、など
// SWIO[1] (Set/Way invalidation override): set/way は arm のキャッシュの用語
//   0b0: set/way 命令でのデータキャッシュインバリデートに影響を与えない
//   0b1: set/way 命令でデータキャッシュはインバリデートされる
// VM[0] (virtualization enable)
//   0b0: EL1/0 向けの stage2 アドレス変換を無効化
//   0b1: EL1/0 向けの stage2 アドレス変換を有効化

#define HCR_E2H         (0 << 34)
#define HCR_RW	    	(1 << 31)
#define HCR_TGE         (0 << 27)
// Asynchronous external Aborts and SError interrupt routing
#define HCR_AMO         (1 << 5)    // routing to EL2
// Physical IRQ routing
#define HCR_IMO         (1 << 4)    // routing to EL2
// Physical FIQ routing
#define HCR_FMO         (1 << 3)    // routing to EL2
#define HCR_SWIO        (1 << 1)
#define HCR_VM          (1 << 0)    // stage 2 translation enable

#define HCR_VALUE \
   ( HCR_TACR | HCR_TID3 | HCR_TID2 | HCR_TID1 | \
     HCR_TWE | HCR_TWI | HCR_E2H | HCR_RW | HCR_TGE | HCR_AMO | \
     HCR_IMO | HCR_FMO | HCR_SWIO | HCR_VM)

// ***************************************
// SCR_EL3, Secure Configuration Register (EL3), Page 2022 of AArch64-Reference-Manual.
// ***************************************

// https://developer.arm.com/documentation/ddi0601/2024-09/AArch32-Registers/SCR--Secure-Configuration-Register
// HCE[8]: hypervior call instruction enable
//   0b0: UNDEFINED at Non-secure EL1. UNPREDICTABLE at EL2.
//   0b1: HVC instructions are enabled at Non-secure EL1 and EL2.
#define SCR_RESERVED			(3 << 4)
#define SCR_RW				    (1 << 10)
#define SCR_HCE                 (1 << 8)    // enable HVC
#define SCR_NS				    (1 << 0)
#define SCR_VALUE	    	    (SCR_RESERVED | SCR_RW | SCR_HCE | SCR_NS)

// ***************************************
// SPSR_EL3, Saved Program Status Register (EL3) Page 288 of AArch64-Reference-Manual.
// ***************************************

// SPSR_EL3
// https://developer.arm.com/documentation/ddi0601/2024-12/AArch64-Registers/SPSR-EL3--Saved-Program-Status-Register--EL3-
//
// When AArch32 is supported and exception taken from AArch32 state:
//   ...
// When exception taken from AArch64 state:
// N[31]: Negative Conddition flag
// Z[30]: Zero Conddition flag
// C[29]: Carry Conddition flag
// V[28]: Ovreflow Conddition flag
// D[9]: Debug exception mask
// A[8]: SError asynchronous exception mask
// I[7]: IRQ asynchronous exception mask
// F[6]: FIQ asynchronous exception mask
// M[4]: Execution state. Set to 0b0, the value of PSTATE.nRW, on taking an exception to EL3 from AArch64 state,
//       and copied to PSTATE.nRW on executing an exception return operation in EL3.
//       AArch32 のときは 0b1 になる
//   0b0: AArch64 execution state.
// M[3:0]: AArch64 Exception level and selected Stack Pointer
//   0b0000: EL0.
//   0b0100: EL1 with SP_EL0 (EL1t).
//   0b0101: EL1 with SP_EL1 (EL1h).
//   0b1000: EL2 with SP_EL0 (EL2t).
//   0b1001: EL2 with SP_EL2 (EL2h).
//   0b1100: EL3 with SP_EL0 (EL3t).
//   0b1101: EL3 with SP_EL3 (EL3h).

#define SPSR_MASK_ALL 			(7 << 6)
#define SPSR_EL2h			    (9 << 0)
#define SPSR_VALUE			    (SPSR_MASK_ALL | SPSR_EL2h)

// SPSR_EL1
// https://developer.arm.com/documentation/102670/0301/AArch64-registers/AArch64-register-descriptions/AArch64-Special-purpose-register-description/SPSR-EL1--Saved-Program-Status-Register--EL1-
//
// SPSR_EL3 と概ね同じ(M[3:0] の値の範囲が異なる)


// ***************************************
// VTCR_EL2, Virtualization Transition Control Register (EL2)
// ***************************************

// https://developer.arm.com/documentation/ddi0601/2024-09/AArch64-Registers/VTCR-EL2--Virtualization-Translation-Control-Register
// NSA[30]: Non-secure stage 2 translation output address space for the Secure EL1&0 translation regime
//   0b0: All stage 2 translation for the Non-secure IPA space of the Secure EL1&0 translation regime access the Secure PA space
//   0b1: All stage 2 translation for the Non-secure IPA space of the Secure EL1&0 translation regime access the Non-secure PA space
// NSW[29]: Non-secure stage 2 translation table address space for the Secure EL1&0 translation regime
//   0b0: All stage 2 translation table walks for the Non-secure IPA space of the Secure EL1&0 translation regime are to the Secure PA space.
//   0b1: All stage 2 translation table walks for the Non-secure IPA space of the Secure EL1&0 translation regime are to the Non-secure PA space.
// VS[19]: VMID Size
//   0b0: 8-bit VMID
//   0b1: 16-bit VMID
// PS[18:16]: physical address size for the second stage of translation
//   0b000: 32 bits, 4GB.
//   0b001: 36 bits, 64GB.
//   0b010: 40 bits, 1TB.
//   0b011: 42 bits, 4TB.
//   0b100: 44 bits, 16TB.
//   0b101: 48 bits, 256TB.
//   0b110: 52 bits, 4PB.
//   0b111: 56 bits, 64PB.
// TG0[15:14]: Granule size for the VTTBR_EL2
//   0b00: 4KB.
//   0b01: 64KB.
//   0b10: 16KB.
// SH0[13:12]: Shareability attribute for memory associated with translation table walks using VTTBR_EL2 or VSTTBR_EL2
//   0b00: Non-shareable.
//   0b10: Outer Shareable.
//   0b11: Inner Shareable.
// ORGN0[11:10]: Outer cacheability attribute for memory associated with translation table walks using VTTBR_EL2 or VSTTBR_EL2.
//   0b00: Normal memory, Outer Non-cacheable.
//   0b01: Normal memory, Outer Write-Back Read-Allocate Write-Allocate Cacheable.
//   0b10: Normal memory, Outer Write-Through Read-Allocate No Write-Allocate Cacheable.
//   0b11: Normal memory, Outer Write-Back Read-Allocate No Write-Allocate Cacheable.
// IRGN0[9:8]: Inner cacheability attribute for memory associated with translation table walks using VTTBR_EL2 or VSTTBR_EL2.
//   0b00: Normal memory, Inner Non-cacheable.
//   0b01: Normal memory, Inner Write-Back Read-Allocate Write-Allocate Cacheable.
//   0b10: Normal memory, Inner Write-Through Read-Allocate No Write-Allocate Cacheable.
//   0b11: Normal memory, Inner Write-Back Read-Allocate No Write-Allocate Cacheable.
// SL0[7:6]: Starting level of the stage 2 translation lookup
//   0b01 and VTCR_EL2.TG0 is 0b00 and VTCR_EL2.SL2 is 0b0, start at level1
//   ...
// T0SZ[5:0]: The size offset of the memory region addressed by VTTBR_EL2. The region size is 2^(64 - T0SZ)
#define VTCR_NSA        (1 << 30)
#define VTCR_NSW        (1 << 29)
#define VTCR_VS         (0 << 19)   // 8bit VMID
#define VTCR_PS         (2 << 16)   // 40bit, 1TB
#define VTCR_TG0        (0 << 14)   // 4KB
#define VTCR_SH0        (3 << 12)   // Inner shareable
#define VTCR_ORGN0      (0 << 10)   // outer write-back ...
#define VTCR_IRGN0      (0 << 8)    // inner write-back ...
#define VTCR_SL0        (1 << 6)    // start at level1?
#define VTCR_T0SZ       (64 - 38)   // 仮想アドレスのサイズは 2^38 = 256GB
#define VTCR_VALUE \
    (VTCR_NSA | VTCR_NSW | VTCR_VS | VTCR_PS | VTCR_TG0 | \
     VTCR_SH0 | VTCR_ORGN0 | VTCR_IRGN0 | VTCR_SL0 | VTCR_T0SZ)

#endif
