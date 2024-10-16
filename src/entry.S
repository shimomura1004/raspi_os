#include "arm/sysregs.h"
#include "entry.h"
#include "sys.h"

	//-------- handle_invalid_entry -------------------------------------------
	// 無効な割込みハンドラを表現するためのマクロ

	.macro handle_invalid_entry el, type
	// レジスタを退避
	kernel_entry \el
	// show_invalid_entry_message 用の3つの引数を準備する
	mov	x0, #\type
	mrs	x1, esr_el1
	mrs	x2, elr_el1
	bl	show_invalid_entry_message
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
	.macro	kernel_entry, el
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

	// 割込みが発生した時の EL の値に応じて分岐
	.if	\el == 0
	// EL0 で割込みが発生した場合は sp_el0 に元の sp の値が保存されるのでそれを控える
	mrs	x21, sp_el0
	.else
	// EL1 で割込みが発生した場合はスタックは再利用するので
	// 単純に先ほどレジスタを控えるために動かした分を戻した値を控えればいい
	add	x21, sp, #S_FRAME_SIZE
	.endif /* \el == 0 */

	// elr_el1 は、例外が発生し EL1 にきたときに、戻り先アドレスが入っているレジスタ
	mrs	x22, elr_el1
	// spsr_el1 は、例外が発生し EL1 にきたときに、元のステータスを保存しているレジスタ
	// たとえば条件フラグとか、割込みのマスク状態など
	mrs	x23, spsr_el1

	// これらもメモリ上に保存 (x30 はリンクレジスタ)
	stp	x30, x21, [sp, #16 * 15] 
	stp	x22, x23, [sp, #16 * 16]
	.endm

	//-------- kernel_exit --------------------------------------------------

	// 基本的には kernel_entry と逆のことをやっているだけ
	.macro	kernel_exit, el
	ldp	x22, x23, [sp, #16 * 16]
	ldp	x30, x21, [sp, #16 * 15] 

	.if	\el == 0
	msr	sp_el0, x21
	.endif /* \el == 0 */

	// kernel_entry で控えたレジスタを戻す
	msr	elr_el1, x22
	// eret で戻る exception level は spsr_el1 に含まれている
	msr	spsr_el1, x23


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
	ventry	sync_invalid_el1t			// Synchronous EL1t
	ventry	irq_invalid_el1t			// IRQ EL1t
	ventry	fiq_invalid_el1t			// FIQ EL1t
	ventry	error_invalid_el1t			// Error EL1t

	ventry	sync_invalid_el1h			// Synchronous EL1h
	ventry	el1_irq					// IRQ EL1h
	ventry	fiq_invalid_el1h			// FIQ EL1h
	ventry	error_invalid_el1h			// Error EL1h

	ventry	el0_sync				// Synchronous 64-bit EL0
	ventry	el0_irq					// IRQ 64-bit EL0
	ventry	fiq_invalid_el0_64			// FIQ 64-bit EL0
	ventry	error_invalid_el0_64			// Error 64-bit EL0

	ventry	sync_invalid_el0_32			// Synchronous 32-bit EL0
	ventry	irq_invalid_el0_32			// IRQ 32-bit EL0
	ventry	fiq_invalid_el0_32			// FIQ 32-bit EL0
	ventry	error_invalid_el0_32			// Error 32-bit EL0

sync_invalid_el1t:
	handle_invalid_entry 1, SYNC_INVALID_EL1t

irq_invalid_el1t:
	handle_invalid_entry 1, IRQ_INVALID_EL1t

fiq_invalid_el1t:
	handle_invalid_entry 1, FIQ_INVALID_EL1t

error_invalid_el1t:
	handle_invalid_entry 1, ERROR_INVALID_EL1t

sync_invalid_el1h:
	handle_invalid_entry 1, SYNC_INVALID_EL1h

fiq_invalid_el1h:
	handle_invalid_entry 1, FIQ_INVALID_EL1h

error_invalid_el1h:
	handle_invalid_entry 1, ERROR_INVALID_EL1h

fiq_invalid_el0_64:
	handle_invalid_entry 0, FIQ_INVALID_EL0_64

error_invalid_el0_64:
	handle_invalid_entry 0, ERROR_INVALID_EL0_64

sync_invalid_el0_32:
	handle_invalid_entry  0, SYNC_INVALID_EL0_32

irq_invalid_el0_32:
	handle_invalid_entry  0, IRQ_INVALID_EL0_32

fiq_invalid_el0_32:
	handle_invalid_entry  0, FIQ_INVALID_EL0_32

error_invalid_el0_32:
	handle_invalid_entry  0, ERROR_INVALID_EL0_32


el1_irq:
	kernel_entry 1 
	bl	handle_irq
	kernel_exit 1 

el0_irq:
	kernel_entry 0 
	bl	handle_irq
	kernel_exit 0 

// 同期割込みハンドラ
// #define ESR_ELx_EC_SHIFT		26
// #define ESR_ELx_EC_SVC64		0x15
// #define ESR_ELx_EC_DABT_LOW	0x24
el0_sync:
	kernel_entry 0
	// exception class を調べるために esr_el1 の ESR_ELx_EC_SHIFT ビット目を確認
	mrs	x25, esr_el1				// read the syndrome register
	lsr	x24, x25, #ESR_ELx_EC_SHIFT		// exception class
	// svc 命令によって発生した割込みか？
	cmp	x24, #ESR_ELx_EC_SVC64			// SVC in 64-bit state
	b.eq	el0_svc
	// データアボートによって発生した割込みか？
	cmp	x24, #ESR_ELx_EC_DABT_LOW		// data abort in EL0
	b.eq	el0_da
	// それ以外の場合は invalid
	handle_invalid_entry 0, SYNC_ERROR

// レジスタのエイリアスの定義
sc_nr	.req	x25					// number of system calls
scno	.req	x26					// syscall number
stbl	.req	x27					// syscall table pointer

// #define __NR_syscalls	 3
el0_svc:
	// stbl は x27 のエイリアス
	adr	stbl, sys_call_table			// load syscall table pointer
	// uxtw は32ビットデータを64ビットにゼロ拡張する命令
	// scno は x26 のエイリアス
	uxtw	scno, w8				// syscall number in w8
	// sc_nr は x25 のエイリアス
	mov	sc_nr, #__NR_syscalls
	// bl: branch with link
	// 割込みを有効にする
	bl	enable_irq
	// システムコール番号がシステムコールの数より小さいかを確認
	cmp     scno, sc_nr                     	// check upper syscall limit
	// システムコール番号が無効なら ni_sys に飛んで invalid とする
	b.hs	ni_sys

	// テーブルのアドレス(stbl)をベースに、
	// インデックス(scno)を8倍(lsl #3)したオフセットを足したところのデータを x16 に入れる
	ldr	x16, [stbl, scno, lsl #3]		// address in the syscall table
	// 指定されたシステムコールを呼び出し
	// x0~x7 は触っていないので、ユーザプログラムがシステムコールを呼んだときにセットした
	// 引数がそのまま渡される
	blr	x16					// call sys_* routine
	b	ret_from_syscall
ni_sys:
	handle_invalid_entry 0, SYSCALL_ERROR
ret_from_syscall:
	// システムコールから戻る処理
	// 割込みを無効にする
	bl	disable_irq
	// ABI に従いシステムコールの戻り値を x0 レジスタに入れたいので
	// kernel_exit が x0 に値を復帰させるメモリ位置に直接今の x0 の値を入れる
	// #define S_X0		0		// offset of x0 register in saved stack frame
	str	x0, [sp, #S_X0]				// returned x0
	kernel_exit 0

// todo: 割込みを有効・無効にするタイミングがよくわからない
// データアボートしたときのハンドラ
el0_da:
	// すぐに割込みを有効にする
	bl	enable_irq
	// far = fault address regiter: データアボートなどが発生した場合に、その仮想アドレスを保持
	mrs	x0, far_el1
	// esr = exception syndrome register: 例外が発生した要因を保持
	mrs	x1, esr_el1
	// mm.c の関数を呼び出し			
	bl	do_mem_abort
	// 戻り値が 0 ならジャンプ
	cmp x0, 0
	b.eq 1f
	// do_mem_abort が失敗した場合は invalid とする
	handle_invalid_entry 0, DATA_ABORT_ERROR
1:
	// 割込みを無効にして戻る
	bl disable_irq				
	kernel_exit 0

// fork で作られたプロセスに CPU 時間が与えられたときの処理
// x19 には指定された関数のポインタが、x20 には引数が入っている
.globl ret_from_fork
ret_from_fork:
	bl	schedule_tail
	cbz	x19, ret_to_user			// not a kernel thread
	mov	x0, x20
	blr	x19
ret_to_user:
	bl disable_irq				
	kernel_exit 0 


.globl err_hang
err_hang: b err_hang
