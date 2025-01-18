#include "arm/sysregs.h"
#include "entry.h"

	//-------- handle_invalid_entry -------------------------------------------
	// 無効な割込みハンドラを表現するためのマクロ

	.macro handle_invalid_entry type
	// レジスタを退避
	kernel_entry
	// show_invalid_entry_message 用の3つの引数を準備する
	mov	x0, #\type
	mrs	x1, esr_el2
	mrs	x2, elr_el2
	mrs x3, far_el2

	bl show_invalid_entry_message
	// err_hang に飛ぶと無限ループする
	b	err_hang
	.endm

	.macro	ventry	label
	// Arm の割込みハンドラは 0x80=128byte ごとに配置されなくてはいけないので
	// .align 7 で 2^7=128byte にアドレスをそろえるよう指示
	.align	7
	b	\label
	.endm

	//-------- kernel_entry --------------------------------------------------
	// #define S_FRAME_SIZE	272	// size of all saved registers

	// ハンドラが呼ばれた直後にやることをまとめたマクロ
	// 具体的にはレジスタの保全を行う
	.macro	kernel_entry
	// 控えるレジスタの容量分 sp をずらしてメモリ上に領域を確保する
	sub	sp, sp, #S_FRAME_SIZE
	// x0-x29 レジスタをスタックに控える (stp は2つのレジスタを一気にメモリに書き込む命令)
	stp	x0, x1, [sp, #16 * 0]
	stp	x2, x3, [sp, #16 * 1]
	stp	x4, x5, [sp, #16 * 2]
	stp	x6, x7, [sp, #16 * 3]
	stp	x8, x9, [sp, #16 * 4]
	stp	x10, x11, [sp, #16 * 5]
	stp	x12, x13, [sp, #16 * 6]
	stp	x14, x15, [sp, #16 * 7]
	stp	x16, x17, [sp, #16 * 8]
	stp	x18, x19, [sp, #16 * 9]
	stp	x20, x21, [sp, #16 * 10]
	stp	x22, x23, [sp, #16 * 11]
	stp	x24, x25, [sp, #16 * 12]
	stp	x26, x27, [sp, #16 * 13]
	stp	x28, x29, [sp, #16 * 14]

	// 元々は割込みが発生した時の EL の値に応じて分岐していたが、今は区別しない
	// EL1 で割込みが発生した場合はスタックは再利用するので
	// 単純に先ほどレジスタを控えるために動かした分を戻した値を控えればいい
	add	x21, sp, #S_FRAME_SIZE

	// elr_el2 は、例外が発生し EL2 にきたときに、戻り先アドレスが入っているレジスタ
	mrs	x22, elr_el2
	// spsr_el2 は、例外が発生し EL2 にきたときに、元のステータスを保存しているレジスタ
	// たとえば条件フラグとか、割込みのマスク状態など
	mrs	x23, spsr_el2

	// これらもメモリ上に保存 (x30 はリンクレジスタ)
	stp	x30, x21, [sp, #16 * 15]
	stp	x22, x23, [sp, #16 * 16]

	// スタックポインタも控えておく
	mrs x24, sp_el0
	mrs x25, sp_el1

	stp x24, x25, [sp, #16 * 17]

	bl vm_leaving_work
	.endm

	//-------- kernel_exit --------------------------------------------------

	// 基本的には kernel_entry と逆のことをやっているだけ
	.macro	kernel_exit
	bl vm_entering_work

	ldp x24, x25, [sp, #16 * 17]

	msr sp_el0, x24
	msr sp_el1, x25

	ldp	x22, x23, [sp, #16 * 16]
	ldp	x30, x21, [sp, #16 * 15]

	// kernel_entry で控えたレジスタを戻す
	msr	elr_el2, x22
	// eret で戻る exception level は spsr_el2 に含まれている
	msr	spsr_el2, x23

	ldp	x0, x1, [sp, #16 * 0]
	ldp	x2, x3, [sp, #16 * 1]
	ldp	x4, x5, [sp, #16 * 2]
	ldp	x6, x7, [sp, #16 * 3]
	ldp	x8, x9, [sp, #16 * 4]
	ldp	x10, x11, [sp, #16 * 5]
	ldp	x12, x13, [sp, #16 * 6]
	ldp	x14, x15, [sp, #16 * 7]
	ldp	x16, x17, [sp, #16 * 8]
	ldp	x18, x19, [sp, #16 * 9]
	ldp	x20, x21, [sp, #16 * 10]
	ldp	x22, x23, [sp, #16 * 11]
	ldp	x24, x25, [sp, #16 * 12]
	ldp	x26, x27, [sp, #16 * 13]
	ldp	x28, x29, [sp, #16 * 14]
	add	sp, sp, #S_FRAME_SIZE
	eret
	.endm

	//-----------------------------------------------------------------------

/*
 * Exception vectors.
 */
 // invalid つきのものは通常は呼ばれないハンドラで、エラー出力して無限ループする
.align	11
.globl vectors
vectors:
	ventry	sync_invalid_el2		// Synchronous EL2
	ventry	irq_invalid_el2			// IRQ EL2
	ventry	fiq_invalid_el2			// FIQ EL2
	ventry	error_invalid_el2		// Error EL2

	ventry	sync_invalid_el2		// Synchronous EL2
	ventry	el2_irq					// IRQ EL2
	ventry	fiq_invalid_el2			// FIQ EL2
	ventry	error_invalid_el2		// Error EL2

	ventry	el01_sync				// Synchronous 64-bit EL0 or EL1
	ventry	el01_irq				// IRQ 64-bit EL0 or EL1
	ventry	fiq_invalid_el01_64		// FIQ 64-bit EL0 or EL1
	ventry	error_invalid_el01_64	// Error 64-bit EL0 or EL1

	ventry	sync_invalid_el01_32	// Synchronous 32-bit EL0 or EL1
	ventry	irq_invalid_el01_32		// IRQ 32-bit EL0 or EL1
	ventry	fiq_invalid_el01_32		// FIQ 32-bit EL0 or EL1
	ventry	error_invalid_el01_32	// Error 32-bit EL0 or EL1

sync_invalid_el2:
	handle_invalid_entry SYNC_INVALID_EL2

irq_invalid_el2:
	handle_invalid_entry IRQ_INVALID_EL2

fiq_invalid_el2:
	handle_invalid_entry FIQ_INVALID_EL2

error_invalid_el2:
	handle_invalid_entry ERROR_INVALID_EL2

fiq_invalid_el01_64:
	handle_invalid_entry FIQ_INVALID_EL01_64

error_invalid_el01_64:
	handle_invalid_entry ERROR_INVALID_EL01_64

sync_invalid_el01_32:
	handle_invalid_entry SYNC_INVALID_EL01_32

irq_invalid_el01_32:
	handle_invalid_entry IRQ_INVALID_EL01_32

fiq_invalid_el01_32:
	handle_invalid_entry FIQ_INVALID_EL01_32

error_invalid_el01_32:
	handle_invalid_entry ERROR_INVALID_EL01_32


el2_irq:
	kernel_entry
	bl	handle_irq
	kernel_exit

el01_irq:
	kernel_entry
	bl	handle_irq
	kernel_exit

// 同期割込みハンドラ
// EL0/1 で同期割込みが発生した場合
el01_sync:
	kernel_entry

	mrs x0, esr_el2		// read the syndrome register
	mrs x1, elr_el2
	mrs x2, far_el2
	mov x3, x8			// hvc number in x8

	bl handle_sync_exception

	kernel_exit

// vmtask_create で作られたタスクに切り替わったときの処理
// x19 には指定された関数のポインタが、x20 には引数が入っている
.globl switch_from_kthread
switch_from_kthread:
	bl	schedule_tail
	mov	x0, x20
	// todo: なぜ以下3つのコピーが必要か不明
	//       kernel_entry のときに以下のようにレジスタを控えているが…
	//   add	x21, sp, #S_FRAME_SIZE
	//   mrs	x22, elr_el2
	//   mrs	x23, spsr_el2
	mov x1, x21
	mov x3, x22
	mov x4, x23
	// x19 (create_initial_sysregs)に無条件分岐する
	blr	x19
	// 割込みを無効にしてカーネル空間から抜ける
	bl disable_irq
	kernel_exit

.globl err_hang
err_hang: b err_hang
