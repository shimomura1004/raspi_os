#include "arm/mmu.h"
#include "arm/sysregs.h"
#include "mm.h"
#include "peripherals/base.h"

// todo: secure monitor の機能を実装する
// https://trustedfirmware-a.readthedocs.io/en/latest/plat/rpi3.html

.section ".text.boot"

// 初期化が終わったことを表すメモリ
.extern initialized_flag

// SCTLR_EL1: system control register: キャッシュや MMU が有効か無効か、など
// HCR_EL2: hypervisor configuration register: ハイパーバイザ関連の機能
// SCR_EL3: secure configuration register: secure monitor
//          どの例外を EL3 でトラップするか指定する他、NS ビットの設定もできる
// SPSR_EL3: saved program status register: condition flag とか
//   condition flag: ゼロかどうか、オーバーフローしたか、…
// ELR_EL3: exception link register: eret で戻る先のアドレス

// リンカスクリプトで .text.boot セクションが 0 番地に置かれる
// ここが OS のスタートポイントになる
.globl _start
_start:
    // コア 0 以外のコアは初期化が終わるまで待機
	mrs	x0, mpidr_el1
	and	x0, x0,#0xFF		// Check processor id
	cbz	x0, el3_entry		// Hang for all non-primary CPU

// コア 2,3 は無限ループさせて止める
//cmp x0, #2
//b.eq sleep
//cmp x0, #3
//b.eq sleep

	// コア1は initialized_flag がセットされるのを待って実行
	ldr x0, =initialized_flag
loop:
	ldr x1, [x0]
	cbz x1, loop
	dsb ish
	b el3_entry
sleep:
	wfi
	b sleep

// EL3 の実質的なスタート
el3_entry:
	// sysregs.h にて定義されている
	// #define SCTLR_VALUE_MMU_DISABLED
	//   (SCTLR_RESERVED | SCTLR_EE_LITTLE_ENDIAN |
	//    SCTLR_I_CACHE_DISABLED | SCTLR_D_CACHE_DISABLED |
	//    SCTLR_MMU_DISABLED)
	// MMU を無効にした状態の規定値を x0 レジスタにロードする
	// 命令キャッシュ・データキャッシュも無効化される
	ldr	x0, =SCTLR_VALUE_MMU_DISABLED

	// 起動直後は EL3 になっているので、EL2 に切り替える
	// EL2 用に設定を登録
	msr	sctlr_el2, x0

	// 二段階アドレス変換の有効化など
	ldr	x0, =HCR_VALUE
	msr	hcr_el2, x0

	// #define SCR_VALUE (SCR_RESERVED | SCR_RW | SCR_NS)
	// RW(register width) は hcr_el2 と同じく 1 にしておかないと 32 ビットになる様子
	// NS(non-secure) は non-secure bit で、1 にすると
	//   Indicates that Exception levels lower than EL3 are
	//   in Non-secure state, so memory accesses
	//   from those Exception levels cannot access Secure memory.
	//   今回はそのまま Normal world に遷移したいので NS は 1 にする
	ldr	x0, =SCR_VALUE
	msr	scr_el3, x0

	// #define SPSR_MASK_ALL (7 << 6)
	// #define SPSR_EL1h (5 << 0)
	// #define SPSR_VALUE (SPSR_MASK_ALL | SPSR_EL1h)
	// MASK_ALL は A,I,F ビットをセットする
	// EL1h は M ビットに EL1h(0b0101) を指定する
	//   EL1h は handler mode を表すらしい
	//   handler mode はいわゆる特権モード
	// EL3 から eret で復帰する(起動後の EL3 から EL1 へ遷移する)ときの
	// 状態を調整している
	// カーネルの処理に遷移するので特権が必要かつ、EL1 で動く必要がある
	// よって EL1h をセットする
	ldr	x0, =SPSR_VALUE
	msr	spsr_el3, x0

	// boot.S 内にある el2_entry のアドレスを elr_el3 に入れる
	// elr_el3 は EL3 から eret するときに使われるアドレスを入れる
	adr	x0, el2_entry
	msr	elr_el3, x0

	// 仮想コアはすべて CPU ID を 0 にする
	// todo: ひとつの VM に複数のコアを割り当てる場合は修正が必要
	ldr x1, =0x80000000
	msr vmpidr_el2, x1

	// 指定した状態(EL2)で、指定したアドレス(el2_entry)に復帰
	eret

// ここから EL2 として実行される
el2_entry:
	// テーブルを用意するのはコア 0 のみ
	mrs	x0, mpidr_el1
	and	x0, x0, #0xFF
	cbz x0, page_table_setup
	b after_page_table_setup

page_table_setup:
	// リンカスクリプトで設定される bss 領域の先頭/末尾を x0/x1 に入れる
	adr	x0, bss_begin
	adr	x1, bss_end
	// bss 領域のサイズ(x1 - x0)を計算し x1 に入れる
	sub	x1, x1, x0
	// bss 領域を 0 クリア
	bl 	memzero

	// EL3 には仮想アドレス機能はない
	// 2段階アドレス変換は、TTBR0_EL1/TTBR1_EL1 -> VTTBR_EL2 の順で変換テーブルを使う
	// EL2 用のページテーブルを準備
	bl 	__create_page_tables

after_page_table_setup:
	// VA_START を足すことでカーネルの仮想アドレスにしている
	mov	x0, #VA_START
	// #define PAGE_SHIFT	 	12
	// #define TABLE_SHIFT 		9
	// #define SECTION_SHIFT	(PAGE_SHIFT + TABLE_SHIFT)
	// #define PAGE_SIZE   		(1 << PAGE_SHIFT)
	// #define SECTION_SIZE		(1 << SECTION_SHIFT)
	// #define LOW_MEMORY      	(2 * SECTION_SIZE)
	// Arm では少し大きなセクション(2MB)という単位でメモリを扱うこともできる
	// aarch64 もスタックは下方向に伸びるので、スタックは 2 * 2MB で 4MB 確保されている
	// sp には仮想アドレスが入っているので MMU が有効になってからじゃないと使えない
	// add	sp, x0, #LOW_MEMORY

	// CPU ID に応じて SP にオフセットをつける (SP = LOW_MEMORY * CPUID)
	mov x1, #LOW_MEMORY
	bl get_cpuid
	add x0, x0, #1
	mul x0, x1, x0
	add sp, x0, #VA_START

	// TTBR(translate table base register)は複数ある: TTBR{0,1}_el{0,1} + TTBR0_el{2,3}
	// TTBR は x86 の cr3 に相当する(risc-v のsatp)
	// TTBR0は 0~256TB までの48-bitの仮想アドレス空間を保持
	// TTBR1のほうは 0xFFFF0000_00000000 から 0xFFFF_FFFFFFFF までの 256TB を保持
	// TTBR0 と TTBR1 で仮想アドレスを分離し、それぞれにユーザアプリとカーネルを置く

	// pg_dir はリンカスクリプトで設定されている
	// __create_page_tables によって、pg_dir にはアドレス変換テーブルの最上位のページのアドレスが入っている
	// __create_page_tables が作るのは単純なリニアマッピング
	// adrp は address of page の略で、pg_dir のページアドレスを x0 に入れる
	adrp	x0, pg_dir
	msr	ttbr0_el2, x0

	// TCR レジスタは MMU を設定するためのレジスタ
	ldr	x0, =(TCR_VALUE)
	msr	tcr_el2, x0

	// VTCR(virtualization translation control register)
	// Stage2 アドレス変換の制御を行うレジスタ
	ldr	x0, =(VTCR_VALUE)
	msr	vtcr_el2, x0

	// MAIR の設定を行う(詳細は mmu.h)
	// MAIR: memory attribute indirection register
	// #define MT_DEVICE_nGnRnE 		0x0
	// #define MT_NORMAL_NC				0x1
	// #define MT_DEVICE_nGnRnE_FLAGS	0x00
	// #define MT_NORMAL_NC_FLAGS  		0x44
	// #define MAIR_VALUE	(MT_DEVICE_nGnRnE_FLAGS << (8 * MT_DEVICE_nGnRnE))
	//                    | (MT_NORMAL_NC_FLAGS << (8 * MT_NORMAL_NC))
	ldr	x0, =(MAIR_VALUE)
	msr	mair_el2, x0

	// EL1 に関連するすべての TLB エントリをクリアする
	tlbi alle1		// TLB Invalidate, ALL EL1

	// MMU を有効にする前にジャンプ先の絶対(仮想)アドレスを控えておく
	// 単に br hypervisor_main とすると、PC に対する hypervisor_main のアドレスへの相対値でジャンプするが、
	// MMU が無効であった場合の相対位置にジャンプしてしまうのでずれる
	// 一度 ldr でアドレスをレジスタに入れると絶対値になり、これであれば MMU 有効化後に適切な値となる
	// todo: MMU が有効になっても相対値でジャンプできそうだけど…
	ldr	x2, =hypervisor_main

	// MMU を有効にする準備
	mov	x0, #SCTLR_MMU_ENABLED
	// MMU を有効にする前にすべての命令を実行完了しておく
	dsb ish
	isb
	// MMU を有効にする
	msr	sctlr_el2, x0
	// これ以降、MMU が有効になった状態で命令が実行されることを確実にする
	isb

	// CPUID を第一引数に入れて hypervisor_main 関数にジャンプ
	bl get_cpuid
	br 	x2

//-------- ここからマクロ定義 ------------------------------------------------
// todo: できれば C にしたい

// AArch64 の仮想アドレス(48bit)の構成
//   [PGD: 9bit][PUD: 9bit][PMD: 9bit][PTE: 9bit][OFFSET: 12bit]
// RPi OS ではセクションマッピングを使う、セクションのサイズは 2MB
//   -> PTE がなくなり、オフセットが21bit(2MB)になる

// RPi3 の RAM は 1GB しかないので、PGD と PUD は1つずつで十分である
// ただ1GB以降の物理アドレスに存在するローカルペリフェラルも同様にリニアマップしたいので
// PMD をもう1つ追加する

// create_pgd_entry を実行すると、4ページ分確保したメモリ領域に、前から順番に
//   [PGD] pg_dir が指すところ
//   [PUD]
//   [PMD] DRAM 用、未初期化
//   [PMD] ローカルペリフェラル用、未初期化
// が確保される
// またセクションマッピングを使うので PTE は不要

// #define PAGE_SHIFT	 		12
// #define TABLE_SHIFT 			9
// #define PGD_SHIFT			PAGE_SHIFT + 3*TABLE_SHIFT
// #define PUD_SHIFT			PAGE_SHIFT + 2*TABLE_SHIFT

//======== create_pgd_entry ===============================================

// ページグローバルディレクトリを作る
// メモリ上に [PGD][PUD][PMD] の順にテーブルが直接並んでいることを前提にしている

.macro	create_pgd_entry, tbl, virt, tmp1, tmp2, tmp3
	// PGD(page global directory) を作る
    //   直後にある PMD を指すエントリが追加される
	create_table_entry \tbl, \virt, PGD_SHIFT, #1, \tmp1, \tmp2, \tmp3
	add	\tbl, \tbl, #PAGE_SIZE

	// PUD(page upper directory) を作る
    //   直後にある PMD を指すエントリ(DRAM 用とローカルペリフェラル用)が追加される
	create_table_entry \tbl, \virt, PUD_SHIFT, #1, \tmp1, \tmp2, \tmp3
    // todo: virt を1ページ分ずらさないといけない
    // todo: virt が 0x0 のままになっているので動かない
    // todo: 応急処置で直接設定
    mov \virt, #0x40000000
	create_table_entry \tbl, \virt, PUD_SHIFT, #2, \tmp1, \tmp2, \tmp3
	add	\tbl, \tbl, #PAGE_SIZE
.endm

// #define PTRS_PER_TABLE	(1 << TABLE_SHIFT) ページ1つあたりのエントリ数(512個)
// #define PAGE_SIZE   		(1 << PAGE_SHIFT)
// #define MM_TYPE_PAGE_TABLE		0x3

//======== create_table_entry ===============================================

// tbl:             エントリを更新するページテーブルのアドレス
// virt:            エントリに追加する仮想アドレス
// shift:           PGD/PUD/PMD のどの部分を更新するかを示すシフト量
// nextpage_offset: 追加するエントリが指す次ページのアドレスが、tbl から見て何ページ先にあるか
// tmp1,2,3:        作業用に使えるレジスタ

// str はストア命令
//   STR   Xt, [base, Xm {,LSL|SXTX {#0 | #3}} ]   // レジスタオフセット
//     Xt: メモリに保存する値を保持するレジスタ
//     base: 書き込み先のベースとなるアドレスを保持するレジスタ
//     Xm: ベースに対するオフセットを保持するレジスタ
//     lsl #3: オフセットを左に3ビットシフト(8倍)することを指示している

.macro	create_table_entry, tbl, virt, shift, nextpage_offset, tmp1, tmp2, tmp3
	// logical shift right
	lsr	\tmp1, \virt, #\shift
	// テーブルインデックスの部分だけマスクして tmp1 に取り出す
	and	\tmp1, \tmp1, #PTRS_PER_TABLE - 1			// table index
	// tmp2 に次のページのアドレスを入れる
    mov \tmp2, #PAGE_SIZE
    mov \tmp3, \nextpage_offset
    mul \tmp2, \tmp2, \tmp3
	add	\tmp2, \tbl, \tmp2
	// アドレスの下位2ビットにフラグを設定し、これがページテーブルであると宣言
	orr	\tmp2, \tmp2, #MM_TYPE_PAGE_TABLE

	// ページエントリを書き込む
	//   tmp1: 更新したいエントリのインデックス(アドレスにするために8倍する必要がある)
    //   tmp2: 書き込む値(PGD を更新するときは PUD のアドレス、PUD を更新するときは PMD のアドレス)
	str	\tmp2, [\tbl, \tmp1, lsl #3]
.endm

//======== create_block_map ===============================================

// PMD テーブルのエントリを準備する

// tbl:     PMD テーブルへのポインタ
// phys:    マップ対象の物理アドレスの開始位置
// start:   マップ先の仮想アドレスの先頭
// end:     マップ先の仮想アドレスの末尾
// flags:   エントリの lower attributes にセットするフラグ

.macro	create_block_map, tbl, phys, start, end, flags, tmp1
	// start のアドレスから PUD のインデックスを取り出す
	lsr	\start, \start, #SECTION_SHIFT
	and	\start, \start, #PTRS_PER_TABLE - 1			// table index
	// end のアドレスから PUD のインデックスを取り出す
	lsr	\end, \end, #SECTION_SHIFT
	and	\end, \end, #PTRS_PER_TABLE - 1				// table end index
	// 対象の物理アドレスを右シフトし、orr 命令で再度左シフトする
	// これにより2MB(21ビット)の境界にそろっていないアドレスでも端数を捨てて変換できるようにしている
	lsr	\phys, \phys, #SECTION_SHIFT
	mov	\tmp1, #\flags
	orr	\phys, \tmp1, \phys, lsl #SECTION_SHIFT			// table entry
	// この段階で、phys は2MBでアラインされ、かつ適切なフラグも設定されている状態になる

9999:
	// エントリをメモリに書き込み
	// create_table_entry と同じで、インデックスをアドレスに変換しつつ格納する
	str	\phys, [\tbl, \start, lsl #3]				// store the entry
	// start を進める(start はアドレスではなくインデックスなので1を足せばいい)
	add	\start, \start, #1					// next entry
	// 物理アドレスも進める
	add	\phys, \phys, #SECTION_SIZE				// next block
	// phys ではなくインデックスで比較するのは、phys の下位ビットにはフラグが設定されているため
	cmp	\start, \end
	// b.ls は less than or same なので、start と end が同じときも 9999 にジャンプする
	// なので end は SECTION_SIZE 分小さい値にしておかないとオーバーランする
	b.ls	9999b
.endm

//=============================================================================

// BCM2837 では RAM が物理アドレスの 0 番地から 1GB 搭載される
// そのうち 0x0000_0000 から 0x3F00_0000 までは DRAM
// 残りの 0x3F00_0000 から 0x4000_0000 まではデバイス
//   BCM2837 ARM Peripherals manual, section 1.2.3 ARM physical addresses
//     Physical addresses range from 0x3F000000 to 0x3FFFFFFF for peripherals.
//   https://cs140e.sergio.bz/docs/BCM2837-ARM-Peripherals.pdf
// それらを仮想アドレス VA_START から始めて DEVICE_BASE までの範囲をマッピングする
__create_page_tables:
	// x30 はリンクレジスタで、bl 命令でジャンプしたときの戻り先を保存している
	// このあと bl 命令でジャンプすると x30 が上書きされてしまうので控えている
	// x29 はだれも使わないことがわかっているのでスタックではなく x29 に控えれば十分
	mov	x29, x30						// save return address

	// pg_dir(リンカスクリプトで指定)のベースアドレスを x0 に代入
	adrp	x0, pg_dir

	// x1 にページディレクトリのサイズ(4KB x 4)を代入
	// #define PAGE_SHIFT	 		12
	// #define PAGE_SIZE   			(1 << PAGE_SHIFT)
	// #define PG_DIR_SIZE			(4 * PAGE_SIZE)
	mov	x1, #PG_DIR_SIZE

	// pg_dir が指す位置から4ページ分を 0 クリアする
	// PGD, PUD, PMDx2 の4つ分、RPi3 の DRAM は1GBで、2MB の倍数なのでセクションマッピングが使える
	// よって PTE はいらない
	bl 	memzero

	// memzero で x0 の値は更新されるので改めて設定
	adrp	x0, pg_dir

	mov	x1, #VA_START
	// create_pgd_entry は以下のように定義されたマクロ
	// 	.macro	create_pgd_entry, tbl, virt, tmp1, tmp2
	// x0 で指定したページテーブルに、x1 のアドレス以降を使ったマッピングを作る
	// x2, x3 は作業用に自由に使っていいレジスタ
	create_pgd_entry x0, x1, x2, x3, x4


	/* Mapping kernel and init stack */
	// 通常の DRAM 領域のマッピング
	// xzr はゼロレジスタ
	mov 	x1, xzr							// start mapping from physical offset 0
	mov 	x2, #VA_START					// first virtual address

	// 通常の DRAM 領域の最後のセクションのアドレスを計算している
	// #define DEVICE_BASE 		0x3F000000
	// SECTION_SIZE は 1<<21 なので、2MB
	ldr	x3, =(VA_START + DEVICE_BASE - SECTION_SIZE)		// last virtual address

	// create_block_map, tbl, phys, start, end, flags, tmp1
	// tbl = x0 = pg_dir
	// phys = x1 = 0
	// start = x2 = VA_START
	// end = x3 = VA_START + DEVICE_BASE - SECTION_SIZE
	// flags = MMU_FLAGS
	// 1GB のうち 0x3f000000 以降はデバイス用の領域なので2回目の create_block_map 呼び出しで対応
	// end(x3) が SECTION_SIZE 分小さくなっているのは、
	//   start=end となったアドレスまでマップしてからループを抜けるようになっているため
	//   つまり1ブロック分小さくしておかないとオーバーランする
	create_block_map x0, x1, x2, x3, MMU_FLAGS, x4

	/* Mapping device memory */
	// デバイス領域のマッピング
	mov 	x1, #DEVICE_BASE                                // start mapping from device base address
	ldr 	x2, =(VA_START + DEVICE_BASE)                   // first virtual address
	ldr	x3, =(VA_START + PHYS_MEMORY_SIZE - SECTION_SIZE)   // last virtual address

	// tbl = x0 = pg_dir
	// phys = x1 = DEVICE_BASE
	// start = x2 = VA_START + DEVICE_BASE
	// end = x3 = VA_START + PHYS_MEMORY_SIZE - SECTION_SIZE
	create_block_map x0, x1, x2, x3, MMU_DEVICE_FLAGS, x4
	add	x0, x0, #PAGE_SIZE

	// 現状 VA_START が 0x0000_0000_0000_0000 なので、
	// ここまでくると、カーネルのアドレス空間(0x0000_0000_0000_0000 以降)に、
	// 物理アドレス 0x0000_0000_0000_0000~0x0000_0000_4000_0000 がマップされている
	// つまり RPi3 のすべての有効な物理アドレスがマップされているということ

    // ローカルペリフェラルは物理アドレス 0x40000000(1GB) 以降にある
    //   0x200000 = 2MB(セクション1つぶん)
    // よって PMD の追加が必要(PMD 1つにつき 1GB まで)
    mov x1, #0x40000000
    ldr x2, =(VA_START + 0x40000000)
    ldr x3, =(VA_START + 0x40200000 - SECTION_SIZE)
    create_block_map x0, x1, x2, x3, MMU_DEVICE_FLAGS, x4

	// x30 に戻りアドレスを入れて ret する
	mov	x30, x29						// restore return address
	ret
