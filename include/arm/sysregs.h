#ifndef _SYSREGS_H
#define _SYSREGS_H

// ***************************************
// SCTLR_EL2, System Control Register (EL2), Page 2025 of AArch64-Reference-Manual.
// ***************************************

// SCTLR: sytem control register
// EE[25]: 0 なら little-endian, 1 なら big-endian
// I[12] (instruction access cacheability control): 0 なら命令キャッシュ不可、1 なら命令キャッシュ可
// C[2] (cacheability control): 0 ならデータキャッシュ不可、1 ならデータキャッシュ可
// M[0] (MMU enable): 0 なら無効、1 なら有効
#define SCTLR_EE                    (0 << 25)
#define SCTLR_I_CACHE_DISABLED      (0 << 12)
#define SCTLR_D_CACHE_DISABLED      (0 << 2)
#define SCTLR_MMU_DISABLED          (0 << 0)
#define SCTLR_MMU_ENABLED           (1 << 0)

#define SCTLR_VALUE_MMU_DISABLED	(SCTLR_EE | SCTLR_I_CACHE_DISABLED | SCTLR_D_CACHE_DISABLED | SCTLR_MMU_DISABLED)

// ***************************************
// HCR_EL2, Hypervisor Configuration Register (EL2), Page 1923 of AArch64-Reference-Manual.
// ***************************************

// HCR: hypervisor configuration register
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
// TWI[13] (traps el0 and el1 exection of wfi instructions to el2)
//   0b0: トラップしない
//   0b1: EL0 か EL1 で WFI 命令を実行すると EL2 でトラップされる
// AMO[5] (physical serror exception routing)
//   0b0: 特に効果なし
//   0b1: physical serror 例外は EL2 で処理される、など
// IMO[4] (physiccal irq routing)
//   0b0: HCR_EL2.TGE の値によって意味が変わる
//   0b1: physical irq interrupt は EL2 で処理される、など
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
#define HCR_TRVM        (1 << 30)
#define HCR_TGE         (0 << 27)
#define HCR_TWI         (1 << 13)
// Asynchronous external Aborts and SError interrupt routing
#define HCR_AMO         (1 << 5)    // routing to EL2
// Physical IRQ routing
#define HCR_IMO         (1 << 4)    // routing to EL2
// Physical FIQ routing
#define HCR_FMO         (1 << 3)    // routing to EL2
#define HCR_SWIO        (1 << 1)
#define HCR_VM          (1 << 0)    // stage 2 translation enable

#define HCR_VALUE   	(HCR_E2H | HCR_RW | HCR_TGE | HCR_TWI | HCR_AMO| HCR_IMO| HCR_FMO | HCR_SWIO | HCR_VM)

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

#define SPSR_MASK_ALL 			(7 << 6)
#define SPSR_EL2h			    (9 << 0)
#define SPSR_VALUE			    (SPSR_MASK_ALL | SPSR_EL2h)

// ***************************************
// ESR_EL2, Exception Syndrome Register (EL2)
// ***************************************

#define ESR_EL2_EC_SHIFT		26
#define ESR_EL2_EC_TRAP_WFx     1
#define ESR_EL2_EC_HVC64        22
#define ESR_EL2_EC_DABT_LOW		36

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
#define VCTR_SH0        (3 << 12)   // Inner shareable
#define VCTR_ORGN0      (1 << 10)   // outer write-back ...
#define VCTR_IRGN0      (1 << 8)    // inner write-back ...
#define VTCR_SL0        (1 << 6)    // start at level1?
#define VTCR_T0SZ       (64 - 38)   // 仮想アドレスのサイズは 2^38 = 256GB
#define VTCR_VALUE \
    (VTCR_NSA | VTCR_NSW | VTCR_VS | VTCR_PS | VTCR_TG0 | \
     VCTR_SH0 | VCTR_ORGN0 | VCTR_IRGN0 | VTCR_SL0 | VTCR_T0SZ)

#endif
