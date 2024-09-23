#include "arm/mmu.h"
#include "arm/sysregs.h"
#include "mm.h"
#include "peripherals/base.h"

.section ".text.boot"

// リンカスクリプトで .text.boot セクションが 0 番地に置かれる
// ここが OS のスタートポイントになる
// raspberry pi 3 では？(a53 では？) 0 番地から実行が始まる？
.globl _start
_start:
    // コア 0 以外のコアは無限ループさせる
	mrs	x0, mpidr_el1		
	and	x0, x0,#0xFF		// Check processor id
	cbz	x0, master		// Hang for all non-primary CPU
	b	proc_hang

// コア 0 以外のコアを無限ループさせて止める
proc_hang: 
	b proc_hang

// これ以降はコア 0 だけで実行される
master:
	// sysregs.h にて定義されている
	// #define SCTLR_VALUE_MMU_DISABLED	
	//   (SCTLR_RESERVED | SCTLR_EE_LITTLE_ENDIAN | 
	//    SCTLR_I_CACHE_DISABLED | SCTLR_D_CACHE_DISABLED |
	//    SCTLR_MMU_DISABLED)
	// MMU を無効にした状態の規定値を x0 レジスタにロードする
	// 命令キャッシュ・データキャッシュも無効化される
	ldr	x0, =SCTLR_VALUE_MMU_DISABLED
	// EL1 用に設定を登録
	msr	sctlr_el1, x0		

	// #define HCR_RW	    			(1 << 31)
	// #define HCR_VALUE			HCR_RW
	ldr	x0, =HCR_VALUE
	// ハイパーバイザ機能は使わないが、マニュアルによると
	// RW ビットが 0 だと
	// Lower levels are all AArch32. This is the reset value.
	// となるようなので 1 にする
	msr	hcr_el2, x0

	// #define SCR_VALUE (SCR_RESERVED | SCR_RW | SCR_NS)
	// RW は hcr_el2 と同じく 1 にしておかないと 32 ビットになる様子
	// NS は non-secure bit で、1 にすると
	//   Indicates that Exception levels lower than EL3 are
	//   in Non-secure state, so memory accesses 
	//   from those Exception levels cannot access Secure memory.
	ldr	x0, =SCR_VALUE
	msr	scr_el3, x0

	// #define SPSR_MASK_ALL (7 << 6)
	// #define SPSR_EL1h (5 << 0)
	// #define SPSR_VALUE (SPSR_MASK_ALL | SPSR_EL1h)
	// MASK_ALL は A,I,F ビットをセットする
	// EL1h は M ビットに EL1h(0b0101) を指定する
	//   EL1h は handler mode を表すらしい
	//   handler mode はいわゆる特権モード
	// EL3 から eret で復帰する(起動後の EL3 から EL1 へ遷移する)ときの
	// 状態を調整している
	// カーネルの処理に遷移するので特権が必要かつ、EL1 で動く必要がある
	// よって EL1h をセットする
	ldr	x0, =SPSR_VALUE
	msr	spsr_el3, x0

	// boot.S 内にある el1_entry のアドレスを elr_el3 に入れる
	// elr_el3 は EL3 から eret するときに使われるアドレスを入れる
	adr	x0, el1_entry		
	msr	elr_el3, x0

	// 指定した状態(EL1h)で、指定したアドレス(el1_entry)に復帰
	eret				

// ここから EL1 として実行される
el1_entry:
	// リンカスクリプトで設定される bss 領域の先頭/末尾を x0/x1 に入れる
	adr	x0, bss_begin
	adr	x1, bss_end
	// bss 領域のサイズ(x1 - x0)を計算し x1 に入れる
	sub	x1, x1, x0
	// bss 領域を 0 クリア
	bl 	memzero

	bl 	__create_page_tables

	mov	x0, #VA_START			
	add	sp, x0, #LOW_MEMORY

	adrp	x0, pg_dir				
	msr	ttbr1_el1, x0

	ldr	x0, =(TCR_VALUE)		
	msr	tcr_el1, x0

	ldr	x0, =(MAIR_VALUE)
	msr	mair_el1, x0


	ldr	x2, =kernel_main

	mov	x0, #SCTLR_MMU_ENABLED				
	msr	sctlr_el1, x0

	br 	x2

	.macro	create_pgd_entry, tbl, virt, tmp1, tmp2
	create_table_entry \tbl, \virt, PGD_SHIFT, \tmp1, \tmp2
	create_table_entry \tbl, \virt, PUD_SHIFT, \tmp1, \tmp2
	.endm

	.macro	create_table_entry, tbl, virt, shift, tmp1, tmp2
	lsr	\tmp1, \virt, #\shift
	and	\tmp1, \tmp1, #PTRS_PER_TABLE - 1			// table index
	add	\tmp2, \tbl, #PAGE_SIZE
	orr	\tmp2, \tmp2, #MM_TYPE_PAGE_TABLE	
	str	\tmp2, [\tbl, \tmp1, lsl #3]
	add	\tbl, \tbl, #PAGE_SIZE					// next level table page
	.endm

	.macro	create_block_map, tbl, phys, start, end, flags, tmp1
	lsr	\start, \start, #SECTION_SHIFT
	and	\start, \start, #PTRS_PER_TABLE - 1			// table index
	lsr	\end, \end, #SECTION_SHIFT
	and	\end, \end, #PTRS_PER_TABLE - 1				// table end index
	lsr	\phys, \phys, #SECTION_SHIFT
	mov	\tmp1, #\flags
	orr	\phys, \tmp1, \phys, lsl #SECTION_SHIFT			// table entry
9999:	str	\phys, [\tbl, \start, lsl #3]				// store the entry
	add	\start, \start, #1					// next entry
	add	\phys, \phys, #SECTION_SIZE				// next block
	cmp	\start, \end
	b.ls	9999b
	.endm

__create_page_tables:
	// x30 はリンクレジスタで、bl 命令でジャンプしたときの戻り先を保存している
	mov	x29, x30						// save return address

	// pg_dir(リンカスクリプトで指定)のベースアドレスを x0 に代入
	adrp	x0, pg_dir
	// x1 にページサイズを代入
	mov	x1, #PG_DIR_SIZE
	// pg_dir が指す位置から1ページ分を 0 クリアする
	bl 	memzero
	// ここでは1ページ分しかクリアしていないが、リンカスクリプトでは
	// 3ページ分の領域が確保されている

	// memzero で x0 の値は更新されるので改めて設定
	adrp	x0, pg_dir
	// #define VA_START 0xffff000000000000
	mov	x1, #VA_START
	// create_pgd_entry は以下のように定義されたマクロ
	// 	.macro	create_pgd_entry, tbl, virt, tmp1, tmp2
	// x0 で指定したページテーブルに、x1 のアドレス以降を使ったマッピングを作る
	// x2, x3 は作業用に自由に使っていいレジスタ
	create_pgd_entry x0, x1, x2, x3

	/* Mapping kernel and init stack*/
	mov 	x1, xzr							// start mapping from physical offset 0
	mov 	x2, #VA_START						// first virtual address
	ldr	x3, =(VA_START + DEVICE_BASE - SECTION_SIZE)		// last virtual address
	create_block_map x0, x1, x2, x3, MMU_FLAGS, x4

	/* Mapping device memory*/
	mov 	x1, #DEVICE_BASE					// start mapping from device base address 
	ldr 	x2, =(VA_START + DEVICE_BASE)				// first virtual address
	ldr	x3, =(VA_START + PHYS_MEMORY_SIZE - SECTION_SIZE)	// last virtual address
	create_block_map x0, x1, x2, x3, MMU_DEVICE_FLAGS, x4

	mov	x30, x29						// restore return address
	ret

