.globl irq_vector_init
irq_vector_init:
	// SPSel: stack pointer select
	//   0b0: use SP_EL0 at all Exception levels
	//   0b1: use SP_ELx for Exception level ELx
	// SPSel の初期値は 1 なので不要
	// msr SPSel, #1

	adr	x0, vectors		// load VBAR_EL2 with virtual
	// vbar: vector base address register
	//   例外発生時に呼び出されるハンドラのアドレスを格納
	msr	vbar_el2, x0	// vector table address
	ret

// 割込みを有効にする
.globl enable_irq
enable_irq:
	// daifclr は割込みマスクビットをクリアするために使うレジスタ
	// DAIF: debug, Asynchronous abort, IRQ, FIQ
	// DAIF レジスタは上記の D,A,I,F のマスクを設定するビット
	// 2ビット目(0b10)が割込みに対応、0 だと有効、1 だと無効
	msr     daifclr, #2
	// SPSel: stack pointer selector
	// 最下位ビットは SP の使い方を指示する
	// 0b0: SP_EL0 をどの EL でも使う
	// 0b1: SP_ELx をそれぞれの EL で使う
	//msr		SPSel, #1
	ret

// 割込みを無効にする
.globl disable_irq
disable_irq:
	msr	daifset, #2
	ret

// 割込みが有効かどうかを返す
// https://developer.arm.com/documentation/ddi0601/2024-12/AArch64-Registers/DAIF--Interrupt-Mask-Bits
// I[7] IRQ mask bit
//   0b0: Exception not masked
//   0b1: Exception masked
.globl is_interrupt_enabled
is_interrupt_enabled:
	mrs x0, daif
	// 7ビット目が割込みマスクビット
	and x0, x0, #0x80
	// 割込みが有効ならマスクされていない、つまり 0
	// このとき非負の値を返したいので、
	eor x0, x0, #0x80
	ret
