// 今の EL レベルを取得
.globl get_el
get_el:
	mrs x0, CurrentEL
	lsr x0, x0, #2
	ret

.globl put32
put32:
	str w1,[x0]
	ret

.globl get32
get32:
	ldr w0,[x0]
	ret

.globl delay
delay:
	subs x0, x0, #1
	bne delay
	ret

// void set_stage2_pgd(unsigned long pgd, unsigned long vmid);
.globl set_stage2_pgd
set_stage2_pgd:
	and x1, x1, #0xff	// 下位8ビットだけを残す
	lsl x1, x1, #48		// 48ビットずらすので、下位8ビットが64ビットの上位8ビットに移動する
	orr x0, x0, x1		// set VMID (アドレスの上位8ビットに VMID を入れる)
	// アドレス空間を切り替え
	// EL2 にはページテーブルが1つしかない
	// VTTBR_EL2 の上位16ビットは VMID を格納する、残りの48ビットはベースアドレス
	msr	vttbr_el2, x0
	// TLB を無効化
	//tlbi vmalle1is
	// out-of-order 実行で勝手に実行しないように
	// data synchronization barrier
  	DSB ISH              // ensure completion of TLB invalidation
	// instruction synchronization barrier
	isb
	ret

// x0 が指すメモリアドレスに保存された値を各システムレジスタに復元する
.globl _set_sysregs
_set_sysregs:
	ldp x1, x2, [x0], #16
	msr sctlr_el1, x1
	msr spsr_el1, x2
	ldp x1, x2, [x0], #16
	msr ttbr0_el1, x1
	msr ttbr1_el1, x2
	ldp x1, x2, [x0], #16
	msr tcr_el1, x1
	msr mair_el1, x2
	ret

// 各システムレジスタの値を取り出し、x0 が指すメモリアドレスに保存する
.globl _get_sysregs
_get_sysregs:
	mrs x1, sctlr_el1
	mrs x2, spsr_el1
	stp x1, x2, [x0], #16
	mrs x1, ttbr0_el1
	mrs x2, ttbr1_el1
	stp x1, x2, [x0], #16
	mrs x1, tcr_el1
	mrs x2, mair_el1
	stp x1, x2, [x0], #16
	ret
