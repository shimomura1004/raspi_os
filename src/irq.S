.globl irq_vector_init
irq_vector_init:
	adr	x0, vectors				// load VBAR_EL1 with virtual
	// vector base address register: 例外発生時に呼び出されるハンドラのアドレスを格納
	msr	vbar_el1, x0				// vector table address
	ret

.globl enable_irq
enable_irq:
	// daifclr は割込みマスクビットをクリアするために使うレジスタ
	msr    daifclr, #2 
	ret

.globl disable_irq
disable_irq:
	msr	daifset, #2
	ret
