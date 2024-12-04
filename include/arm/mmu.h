#ifndef _MMU_H
#define _MMU_H

// nG[11] (not global) : TLB でどう扱われるかを表す
// AF[10] (Access flag)
// SH[9:8] (Shareability field)
// AP[7:6] (Access Permissions)
// NS[5] (Non-seccure) : このアドレスが secure か non-secure かを表す
// AttrIndx[4:2] : Stage 1 memory attributes index

#define MM_TYPE_PAGE_TABLE		0x3
#define MM_TYPE_PAGE 			0x3
#define MM_TYPE_BLOCK			0x1

#define MM_ACCESS			    (0x1 << 10)
#define MM_nG                   (0x0 << 11)
#define MM_SH                   (0x3 << 8)
#define MM_ACCESS_PERMISSION	(0x01 << 6)

// MAIR_EL1 レジスタに設定する値
// MAIR_EL1 レジスタは8個のパートに分かれており、ページの属性を入れておく
// 各ページは変換テーブルのエントリで MAIR のどのパートを使うかを指定する
// つまりシステム全体でメモリの属性は8パターンしかない
// raspvisor では以下の2パターンを準備している
//   DEVICE_nGnRnE       デバイスメモリ
//   NORMAL_CACHEABLE    通常のメモリで、キャッシュも可能
// https://developer.arm.com/documentation/ddi0601/2024-09/AArch64-Registers/MAIR-EL1--Memory-Attribute-Indirection-Register--EL1-
/*
 * Memory region attributes:
 *
 *   n = AttrIndx[2:0]
 *			n	MAIR
 *   DEVICE_nGnRnE      000 00000000
 *   NORMAL_CACJEABLE   001 11111111
 */
#define MT_DEVICE_nGnRnE            0x0
#define MT_NORMAL_CACHEABLE         0x1

#define MT_DEVICE_nGnRnE_FLAGS      0x00
#define MT_NORMAL_CACHEABLE_FLAGS   0xff

#define MAIR_VALUE \
    (MT_DEVICE_nGnRnE_FLAGS << (8 * MT_DEVICE_nGnRnE)) | \
    (MT_NORMAL_CACHEABLE_FLAGS << (8 * MT_NORMAL_CACHEABLE))

#define MMU_FLAGS \
    (MM_TYPE_BLOCK | (MT_NORMAL_CACHEABLE << 2) | MM_nG | MM_ACCESS)
#define MMU_DEVICE_FLAGS \
    (MM_TYPE_BLOCK | (MT_DEVICE_nGnRnE << 2) | MM_nG | MM_ACCESS)

#define MM_STAGE2_ACCESS        (1 << 10)
#define MM_STAGE2_SH            (3 << 8)
#define MM_STAGE2_MEMATTR       (1 << 0)

#define MMU_STAGE2_PAGE_FLAGS \
    (MM_TYPE_PAGE | MM_STAGE2_ACCESS | MM_STAGE2_SH | MM_STAGE2_MEMATTR)

#define TCR_T0SZ			(64 - 48)
#define TCR_TG0_4K			(0 << 14)
#define TCR_VALUE			(TCR_T0SZ | TCR_TG0_4K)

#endif
