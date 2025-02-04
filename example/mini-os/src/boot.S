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
	adr	x0, bss_begin
	adr	x1, bss_end
	sub	x1, x1, x0
	bl 	memzero

	bl 	__create_page_tables

	mov	x0, #VA_START
	add	sp, x0, #LOW_MEMORY

	adrp	x0, pg_dir
	msr	ttbr1_el1, x0

	// EL1 のカーネルのアドレス空間は低アドレス域にあるので ttbr0 に設定が必要
	// todo: カーネルは高アドレス域にあるべきでは？なぜそうなっていない？
	//       elf は 0xffff_... に配置されるようになっている、objcopy でずれている？
	//       elf ローダを作って kernel8.elf をロードすれば解決できると思われる
	adrp	x0, pg_dir_high
	msr ttbr0_el1, x0

	ldr	x0, =(TCR_VALUE)
	msr	tcr_el1, x0

	ldr	x0, =(MAIR_VALUE)
	msr	mair_el1, x0

	ldr	x2, =kernel_main

	mov	x0, #SCTLR_MMU_ENABLED
	//mov	x0, #SCTLR_MMU_DISABLED
mov x8, x2
hvc #0
adr x8, .
hvc #0

  dsb ish
  isb
// todo: MMU_DISABLE に設定する場合は動いているので、レジスタにアクセスすること自体は問題ない
//       ここまで中間物理アドレスで動いているのでステージ2アドレス変換は問題ない
//       MMU_ENABLE にするとステージ1アドレス変換が有効になる
//       ゲスト OS によるアドレス変換が失敗している？ベアメタルでは動いているはず
//       命令フェッチに失敗している？
//         -> IABT_LOW を追加してログを出してみたが反応なし
//       IPA->PA のマッピングログを見ると、0x6000 から 0x85000 まで
//         0x1000 単位で128個のページがマップされている
//         -> 今実行しているアドレス 0x60 くらいや kernel_main の 0x1b74 が範囲外になっているかも
//         -> ログの埋め込み漏れ、ちゃんとマップされていた
//       なぜか PC が 0x200 に飛ぶ -> 0x200 は synchronous 例外が EL2 で発生したときに飛ぶアドレス
//       クラッシュする寸前、割込みベクタである vectors の中身がすべて 0 になっている
//       まずベアメタルで動くことを確認できたバイナリを用意する
//       VASTART の違いがあやしそうだが…
	msr	sctlr_el1, x0
isb
wfi
	br 	x2

	.macro	create_pgd_entry, tbl, virt, tmp1, tmp2
	create_table_entry \tbl, \virt, PGD_SHIFT, \tmp1, \tmp2
	create_table_entry \tbl, \virt, PUD_SHIFT, \tmp1, \tmp2
	.endm

	.macro	create_table_entry, tbl, virt, shift, tmp1, tmp2
mov x8, \virt
hvc #0
mov x8, #\shift
hvc #0
	lsr	\tmp1, \virt, #\shift
	and	\tmp1, \tmp1, #PTRS_PER_TABLE - 1			// table index
	add	\tmp2, \tbl, #PAGE_SIZE
	orr	\tmp2, \tmp2, #MM_TYPE_PAGE_TABLE
	str	\tmp2, [\tbl, \tmp1, lsl #3]
	add	\tbl, \tbl, #PAGE_SIZE					// next level table page
	.endm

	.macro	create_block_map, tbl, phys, start, end, flags, tmp1
mov x8, 12345
hvc #0
mov x8, \phys
hvc #0
mov x8, \start
hvc #0
mov x8, \end
hvc #0
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
mov x8, \start
hvc #0
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


	// 下位アドレス用のページテーブルを準備
	// todo: flat binary にするとカーネルが 0x0 に配置されるので必要になる
	adrp x0, pg_dir_high
	ldr	 x1, =0x0000000000000000
	create_pgd_entry x0, x1, x2, x3

	/* Mapping kernel and init stack */
	mov  x1, xzr
	ldr  x2, =0x0000000000000000
	ldr  x3, =(0x0000000000000000 + DEVICE_BASE - SECTION_SIZE)

	create_block_map x0, x1, x2, x3, MMU_FLAGS, x4

	/* Mapping device memory */
	mov  x1, #DEVICE_BASE
	ldr  x2, =(0x0000000000000000 + DEVICE_BASE)
	ldr  x3, =(0x0000000000000000 + PHYS_MEMORY_SIZE - SECTION_SIZE)
	create_block_map x0, x1, x2, x3, MMU_DEVICE_FLAGS, x4


	mov	x30, x29						// restore return address
	ret
