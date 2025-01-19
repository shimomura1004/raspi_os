// todo: サイズが8の倍数でない場合は誤作動する
// todo: コピー元とコピー先の領域が重複していると誤作動する
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

	tlbi vmalls12e1
	// out-of-order 実行で勝手に実行しないように
	// data synchronization barrier
	dsb ish				// ensure completion of TLB invalidation
	// instruction synchronization barrier
	isb

	ret

.globl set_sp_el2
set_sp_el2:
	msr sp_el2, x0
	ret

// x0 が指すメモリアドレスに保存された値を各システムレジスタに復元する
// sched.h で定義された struct cpu_sysregs のメンバの並び順に依存する
.globl _set_sysregs
_set_sysregs:
	// 2変数分読みだしてセットする、の繰り返し
	ldp x1, x2, [x0], #16
	msr sctlr_el1, x1
	msr ttbr0_el1, x2
	ldp x1, x2, [x0], #16
	msr ttbr1_el1, x1
	msr tcr_el1, x2
	ldp x1, x2, [x0], #16
	msr esr_el1, x1
	msr far_el1, x2
	ldp x1, x2, [x0], #16
	msr afsr0_el1, x1
	msr afsr1_el1, x2
	ldp x1, x2, [x0], #16
	msr mair_el1, x1
	msr amair_el1, x2
	ldp x1, x2, [x0], #16
	msr contextidr_el1, x1
	msr cpacr_el1, x2
	ldp x1, x2, [x0], #16
	msr elr_el1, x1
	msr fpcr, x2
	ldp x1, x2, [x0], #16
	msr fpsr, x1
	msr vpidr_el2, x2		// for virtualization
	ldp x1, x2, [x0], #16
	msr vmpidr_el2, x1		// for virtualization
	msr par_el1, x2
	ldp x1, x2, [x0], #16
	msr sp_el0, x1
	msr sp_el1, x2
	ldp x1, x2, [x0], #16
	msr spsr_el1, x1
	msr tpidr_el0, x2
	ldp x1, x2, [x0], #16
	msr tpidr_el1, x1
	msr tpidrro_el0, x2
	// 最後は1変数しかない
	ldr x1, [x0]
	mrs x1, vbar_el1
  	// TTBR を復元してメモリ空間が変わるので、念のためブロックしている？　
	dsb ish
	isb
	ret

// 各システムレジスタの値を取り出し、x0 が指すメモリアドレスに保存する
.globl _get_sysregs
_get_sysregs:
	mrs x1, sctlr_el1
	mrs x2, ttbr0_el1
	stp x1, x2, [x0], #16
	mrs x1, ttbr1_el1
	mrs x2, tcr_el1
	stp x1, x2, [x0], #16
	mrs x1, esr_el1
	mrs x2, far_el1
	stp x1, x2, [x0], #16
	mrs x1, afsr0_el1
	mrs x2, afsr1_el1
	stp x1, x2, [x0], #16
	mrs x1, mair_el1
	mrs x2, amair_el1
	stp x1, x2, [x0], #16
	mrs x1, contextidr_el1
	mrs x2, cpacr_el1
	stp x1, x2, [x0], #16
	mrs x1, elr_el1
	mrs x2, fpcr
	stp x1, x2, [x0], #16
	mrs x1, fpsr
	mrs x2, midr_el1		// todo: 読み出し時は midr_el1 だけど書き込み時は vpidr_el2
	stp x1, x2, [x0], #16
	mrs x1, mpidr_el1		// todo: 読み出し時は mpidr_el1 だけど書き込み時は vmpidr_el2
	mrs x2, par_el1
	stp x1, x2, [x0], #16
	mrs x1, sp_el0
	mrs x2, sp_el1
	stp x1, x2, [x0], #16
	mrs x1, spsr_el1
	mrs x2, tpidr_el0
	stp x1, x2, [x0], #16
	mrs x1, tpidr_el1
	mrs x2, tpidrro_el0
	stp x1, x2, [x0], #16
	mrs x1, vbar_el1		// set_sysregs で戻すのはここまで
	mrs x2, actlr_el1		// ここから先は控えるだけ
	stp x1, x2, [x0], #16
	mrs x1, id_pfr0_el1
	mrs x2, id_pfr1_el1
	stp x1, x2, [x0], #16
	mrs x1, id_mmfr0_el1
	mrs x2, id_mmfr1_el1
	stp x1, x2, [x0], #16
	mrs x1, id_mmfr2_el1
	mrs x2, id_mmfr3_el1
	stp x1, x2, [x0], #16
	mrs x1, id_isar0_el1
	mrs x2, id_isar1_el1
	stp x1, x2, [x0], #16
	mrs x1, id_isar2_el1
	mrs x2, id_isar3_el1
	stp x1, x2, [x0], #16
	mrs x1, id_isar4_el1
	mrs x2, id_isar5_el1
	stp x1, x2, [x0], #16
	mrs x1, mvfr0_el1
	mrs x2, mvfr1_el1
	stp x1, x2, [x0], #16
	mrs x1, mvfr2_el1
	mrs x2, id_aa64pfr0_el1
	stp x1, x2, [x0], #16
	mrs x1, id_aa64pfr1_el1
	mrs x2, id_aa64dfr0_el1
	stp x1, x2, [x0], #16
	mrs x1, id_aa64dfr1_el1
	mrs x2, id_aa64isar0_el1
	stp x1, x2, [x0], #16
	mrs x1, id_aa64isar1_el1
	mrs x2, id_aa64mmfr0_el1
	stp x1, x2, [x0], #16
	mrs x1, id_aa64mmfr1_el1
	mrs x2, id_aa64afr0_el1
	stp x1, x2, [x0], #16
	mrs x1, id_aa64afr1_el1
	mrs x2, ctr_el0
	stp x1, x2, [x0], #16
	mrs x1, ccsidr_el1
	mrs x2, clidr_el1
	stp x1, x2, [x0], #16
	mrs x1, csselr_el1
	mrs x2, aidr_el1
	stp x1, x2, [x0], #16
	mrs x1, revidr_el1
	mrs x2, cntkctl_el1
	stp x1, x2, [x0], #16
	mrs x1, cntp_ctl_el0
	mrs x2, cntp_cval_el0
	stp x1, x2, [x0], #16
	mrs x1, cntp_tval_el0
	mrs x2, cntv_ctl_el0
	stp x1, x2, [x0], #16
	mrs x1, cntv_cval_el0
	mrs x2, cntv_tval_el0
	stp x1, x2, [x0], #16
	ret

.globl assert_vfiq
assert_vfiq:
	// HCR_EL2.VF(Virtual FIQ) をセットする
	mrs x0, hcr_el2
	orr x1, x0, #0x40
	msr hcr_el2, x1
	ret

.globl assert_virq
assert_virq:
	// HCR_EL2.VI(Virtual IRQ) をセットする
	mrs x0, hcr_el2
	orr x1, x0, #0x80
	msr hcr_el2, x1
	ret

.globl assert_vserror
assert_vserror:
	// HCR_EL2.VSI(Virtual SError) をセットする
	mrs x0, hcr_el2
	orr x1, x0, #0x100
	msr hcr_el2, x1
	ret

.globl clear_vfiq
clear_vfiq:
	mrs x0, hcr_el2
	// bic(bit clear)
	//   bic x0, x1, x2: x0 = x1 & ~x2
	// つまり hcr_el2 の VF を 0 にしたものを x1 に入れる
	bic x1, x0, #0x40
	msr hcr_el2, x1
	ret

.globl clear_virq
clear_virq:
	mrs x0, hcr_el2
	bic x1, x0, #0x80
	msr hcr_el2, x1
	ret

.globl clear_vserror
clear_vserror:
	mrs x0, hcr_el2
	bic x1, x0, #0x100
	msr hcr_el2, x1
	ret

// 使っていない
// 引数として仮想アドレスを取り、Stage1 と 2 のアドレス変換を行った値を返す 
.globl do_at
do_at:
	// x0 に入った仮想アドレスを変換
	at s12e1r, x0
	// par_el1 に at 命令の結果が入る
	mrs x0, par_el1
	ret
