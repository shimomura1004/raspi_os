.globl irq_vector_init
irq_vector_init:
	adr	x0, vectors				// load VBAR_EL1 with virtual
	// vector base address register: 例外発生時に呼び出されるハンドラのアドレスを格納
	msr	vbar_el1, x0				// vector table address
	ret

// 割込みを有効にする
.globl enable_irq
enable_irq:
	// daifclr は割込みマスクビットをクリアするために使うレジスタ
	// DAIF: debug, Asynchronous abort, IRQ, FIQ
	// DAIF レジスタは上記の D,A,I,F のマスクを設定するビット
	// 2ビット目(0b10)が割込みに対応、0 だと有効、1 だと無効
	msr    daifclr, #2 
	ret

// 割込みを無効にする
.globl disable_irq
disable_irq:
	msr	daifset, #2
	ret
