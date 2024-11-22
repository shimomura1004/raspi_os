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

.globl set_stage2_pgd
set_stage2_pgd:
	// アドレス空間を切り替え
	msr	vttbr_el2, x0
	// TLB を無効化
	tlbi vmalle1is
	// out-of-order 実行で勝手に実行しないように
	// data synchronization barrier
  	DSB ISH              // ensure completion of TLB invalidation
	// instruction synchronization barrier
	isb
	ret
