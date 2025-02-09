#include "arm/mmu.h"
#include "arm/sysregs.h"
#include "mm.h"
#include "peripherals/base.h"

.section ".text.boot"

.globl _start
_start:
	mrs	x0, mpidr_el1
	and	x0, x0,#0xFF		// Check processor id
	cbz	x0, master		// Hang for all non-primary CPU
	b	proc_hang

proc_hang:
	b proc_hang

master:
	ldr	x0, =SCTLR_VALUE_MMU_DISABLED
	msr	sctlr_el1, x0

el1_entry:
	// この BSS 領域のゼロ初期化のときにマッピングしていない領域にアクセスが発生
	// mem abort が発生してハイパーバイザで stage2 アドレス変換マップが作られる
	// ただし stage1 の変換マップはまだ作られていない(この時点では MMU は無効なので動作はする)
	adr	x0, bss_begin
	adr	x1, bss_end
	sub	x1, x1, x0
	bl 	memzero

	// __create_page_tables 内の create_block_map で stage1 の変換マップが作られる
	bl 	__create_page_tables

	mov	x0, #VA_START
	add	sp, x0, #LOW_MEMORY

	adrp	x0, pg_dir
	msr	ttbr1_el1, x0

	// EL1 のカーネルのアドレス空間は低アドレス域にあるので ttbr0 に設定が必要
	// todo: カーネルは高アドレス域にあるべきでは？なぜそうなっていない？
	//       elf は 0xffff_... に配置されるようになっている、objcopy でずれている？
	//       elf ローダを作って kernel8.elf をロードすれば解決できると思われる
	adrp	x0, pg_dir
	msr ttbr0_el1, x0

	ldr	x0, =(TCR_VALUE)
	msr	tcr_el1, x0

	ldr	x0, =(MAIR_VALUE)
	msr	mair_el1, x0

	ldr	x2, =kernel_main

	mov	x0, #SCTLR_MMU_ENABLED

  dsb ish
  isb
	msr	sctlr_el1, x0
isb
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

9999:
// // マッピングをログ出力
// mov x8, \start
// and x9, \phys, #PAGE_MASK
// mov x10, #SECTION_SIZE
// hvc #3

	// [\tbl, \start, lsl #3]
	//   テーブルのベースアドレス(tbl)にインデックス(start)の8倍(3ビット左シフト)する
	// str phys [\tbl, \start, lsl #3]
	//   phys の値を [\tbl, \start, lsl #3] が指すアドレスに書き込む
	//   つまりページエントリを書き込んでいる
	str	\phys, [\tbl, \start, lsl #3]				// store the entry
	// インデックスと物理アドレスをインクリメントしている
	// つまりリニアにマッピングしている
	add	\start, \start, #1					// next entry
	add	\phys, \phys, #SECTION_SIZE				// next block
	cmp	\start, \end
	b.ls	9999b
	.endm

__create_page_tables:
	mov	x29, x30						// save return address

	adrp	x0, pg_dir
	mov	x1, #PG_DIR_SIZE
	bl 	memzero

	adrp	x0, pg_dir
	mov	x1, #VA_START
	create_pgd_entry x0, x1, x2, x3

	/* Mapping kernel and init stack */
	mov 	x1, xzr							// start mapping from physical offset 0
	mov 	x2, #VA_START						// first virtual address
	ldr	x3, =(VA_START + DEVICE_BASE - SECTION_SIZE)		// last virtual address
	create_block_map x0, x1, x2, x3, MMU_FLAGS, x4

	/* Mapping device memory */
	mov 	x1, #DEVICE_BASE					// start mapping from device base address
	ldr 	x2, =(VA_START + DEVICE_BASE)				// first virtual address
	ldr	x3, =(VA_START + PHYS_MEMORY_SIZE - SECTION_SIZE)	// last virtual address
	create_block_map x0, x1, x2, x3, MMU_DEVICE_FLAGS, x4

	mov	x30, x29						// restore return address
	ret
