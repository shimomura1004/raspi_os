.globl memcpy
memcpy:
	ldr x3, [x1], #8
	str x3, [x0], #8
	subs x2, x2, #8
	b.gt memcpy
	ret

.globl memzero
// x0: 開始アドレス
// x1: バイト数
memzero:
	// xzr は常に 0 を保持するレジスタ
	// x0 が指すアドレスに 0 を書き込み、 x0 を 8 バイト進める
	str xzr, [x0], #8
	// x1 = x1 - 8
	subs x1, x1, #8
	// x1 が 0 より大きい場合はもう一度
	b.gt memzero
	ret

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
	// prepare VMID
	and x1, x1, #0xff	// 下位8ビットだけを残す
	lsl x1, x1, #48		// 48ビットずらすので、下位8ビットが64ビットの上位8ビットに移動する
	// set VMID
	orr x0, x0, x1		// アドレスの上位8ビットに VMID を入れる
	// アドレス空間を切り替え
	// EL2 にはページテーブルが1つしかない
	// VTTBR_EL2 の上位16ビットは VMID を格納する、残りの48ビットはベースアドレス
	msr	vttbr_el2, x0

	// 以下の4この命令のセットで、TLB をインバリデートできる
	// dsb: data synchronization barrier
	//   dsb 命令よりあとに記述された命令は、この命令が終わるまで実行されない
	//   SY: full system DSB operation, this is the default and can be omitted
	//   ST: DSB operation that waits only for stores to complete
	//   ISH: DSB operation only to the inner shareable domain
	//   ISHST: DST operation that waits only for stores to complete, and only to the inner shareable domain.
	dsb ishst
	tlbi vmalls12e1is
	// out-of-order 実行で勝手に実行しないように
	// data synchronization barrier
	dsb ish				// ensure completion of TLB invalidation
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
	// TTBR を復元してメモリ空間が変わるので、念のためブロックしている？　
	dsb ish
	isb
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

.globl _get_spsr_el2
_get_spsr_el2:
	mrs x0, spsr_el2
	ret

.globl _get_sctlr_el1
_get_sctlr_el1:
	mrs x0, sctlr_el1
	ret

.globl _get_vttbr_el2
_get_vttbr_el2:
	mrs x0, vttbr_el2
	ret

.globl _get_id_aa64mmfr0_el1
_get_id_aa64mmfr0_el1:
	// id_aa64mmfr0_el1: information about the implemented memory model and memory management support in the AArch64 Execution state.
	mrs x0, id_aa64mmfr0_el1
	ret

// 引数として仮想アドレスを取り、Stage1 と 2 のアドレス変換を行った値を返す 
.globl do_at
do_at:
	// x0 に入った仮想アドレスを変換
	at s12e1r, x0
	// par_el1 に at 命令の結果が入る
	mrs x0, par_el1
	ret
