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

#define SCR_RESERVED			(3 << 4)
#define SCR_RW				    (1 << 10)
#define SCR_NS				    (1 << 0)
#define SCR_VALUE	    	    (SCR_RESERVED | SCR_RW | SCR_NS)

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

#define VTCR_NSA        (1 << 30)
#define VTCR_NSW        (1 << 29)
#define VTCR_VS         (1 << 19)
#define VTCR_PS         (5 << 16)
#define VTCR_TG0        (0 << 14)
#define VTCR_SL0        (2 << 6)
#define VTCR_T0SZ       (64 - 48)
#define VTCR_VALUE      (VTCR_NSA | VTCR_NSW | VTCR_VS | VTCR_PS | VTCR_TG0 | VTCR_SL0 | VTCR_T0SZ)

#endif
