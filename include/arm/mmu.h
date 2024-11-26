#ifndef _MMU_H
#define _MMU_H

#define MM_TYPE_PAGE_TABLE		0x3
#define MM_TYPE_PAGE 			0x3
#define MM_TYPE_BLOCK			0x1
#define MM_ACCESS			    (0x1 << 10)
#define MM_ACCESS_PERMISSION	(0x01 << 6)
#define MM_nG                   (0x0 << 11)

// MAIR_EL1 レジスタに設定する値
// MAIR_EL1 レジスタは8個のパートに分かれており、ページの属性を入れておく
// 各ページは変換テーブルのエントリで MAIR のどのパートを使うかを指定する
// つまりシステム全体でメモリの属性は8パターンしかない
// RPi OS では2個パターン(DEVICE_nGnRnE と NORMAL_NC)しか使わない
//   DEVICE_nGnRnE  デバイスメモリ
//   NORMAL_NC      通常のメモリで、キャッシュ不可
/*
 * Memory region attributes:
 *
 *   n = AttrIndx[2:0]
 *			n	MAIR
 *   DEVICE_nGnRnE	000	00000000
 *   NORMAL_NC		001	01000100
 */
#define MT_DEVICE_nGnRnE 		0x0
#define MT_NORMAL_NC			0x1
#define MT_DEVICE_nGnRnE_FLAGS		0x00
#define MT_NORMAL_NC_FLAGS  		0x44
#define MAIR_VALUE			(MT_DEVICE_nGnRnE_FLAGS << (8 * MT_DEVICE_nGnRnE)) | (MT_NORMAL_NC_FLAGS << (8 * MT_NORMAL_NC))

#define MMU_FLAGS	 		(MM_TYPE_BLOCK | (MT_NORMAL_NC << 2) | MM_nG | MM_ACCESS)
#define MMU_DEVICE_FLAGS	(MM_TYPE_BLOCK | (MT_DEVICE_nGnRnE << 2) | MM_nG | MM_ACCESS)

#define MM_STAGE2_ACCESS        (1 << 10)
#define MM_STAGE2_MEMATTR       (1 << 0)
#define MMU_STAGE2_PAGE_FLAGS   (MM_TYPE_PAGE | MM_STAGE2_ACCESS | MM_STAGE2_MEMATTR)

#define TCR_T0SZ			(64 - 48)
#define TCR_TG0_4K			(0 << 14)
#define TCR_VALUE			(TCR_T0SZ | TCR_TG0_4K)

#endif
