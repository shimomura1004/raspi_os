.globl irq_vector_init
irq_vector_init:
	adr	x0, vectors				// load VBAR_EL2 with virtual
	// vector base address register: 例外発生時に呼び出されるハンドラのアドレスを格納
	msr	vbar_el2, x0				// vector table address
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
	msr		SPSel, #1
	ret

// 割込みを無効にする
.globl disable_irq
disable_irq:
	msr	daifset, #2
	ret
