#include "mm.h"
#include "arm/mmu.h"
#include "utils.h"
#include "debug.h"
#include "board.h"
#include "vm.h"
#include "spinlock.h"

// ページの使用状況を表す領域
static unsigned short mem_map [ PAGING_PAGES ] = {0,};
static struct spinlock mm_lock;

void mm_init() {
	init_lock(&mm_lock, "mm_lock");
}

// ハイパーバイザで使うためのページを確保し、その仮想アドレスを返す
// RPi OS では "カーネルのアドレス空間" はないのでマッピングの追加は行わない
unsigned long allocate_page() {
	// 未使用ページを探す
	unsigned long page = get_free_page();
	if (page == 0) {
		return 0;
	}
	// RPi OS はリニアマッピング
	// 単純に仮想アドレス空間の開始アドレスをオフセットとして足すと仮想アドレスになる
	return page + VA_START;
}

// VM で使うためのページを確保してマッピングし、ハイパーバイザ上の仮想アドレスを返す
// つまり、ハイパーバイザ上でこのアドレスに書き込むことで、確保したメモリにアクセスできるということ
unsigned long allocate_vm_page(struct vm_struct *vm, unsigned long ipa) {
	// 未使用ページを探す、page は仮想アドレスではなくオフセット
	unsigned long page = get_free_page();
	if (page == 0) {
		return 0;
	}
	// 新たに確保したページをこの VM のアドレス空間にマッピングする
	map_stage2_page(vm, ipa, page, MMU_STAGE2_PAGE_FLAGS);
	// INFO("VTTBR0_EL2(VMID %d): IPA 0x%lx(0x%lx in full) -> PA 0x%lx (allocate_vm_page)",
	// 	 current_cpu_core()->current_vm->vmid, ipa & 0xffffffffffff, ipa, page);

	// 新たに確保したページの仮想アドレスを返す(リニアマッピングなのでオフセットを足すだけ)
	return page + VA_START;
}

void set_vm_page_notaccessable(struct vm_struct *vm, unsigned long va) {
	map_stage2_page(vm, va, 0, MMU_STAGE2_MMIO_FLAGS);
// if (current_cpu_core()->current_vm->vmid != 0)INFO("VA 0x%lx -> IPA 0x%lx -> PA 0x%lx (set_vm_page_notaccessable)", va, get_ipa(va), 0);
}

// 未使用のページを探してその場所(DRAM 内のオフセット)を返す
unsigned long get_free_page()
{
	acquire_lock(&mm_lock);

	for (int i = 0; i < PAGING_PAGES; i++){
		if (mem_map[i] == 0){
			// 未使用領域を見つけたらフラグを立てる
			mem_map[i] = 1;
			unsigned long page = LOW_MEMORY + i*PAGE_SIZE;
			// RPi OS はリニアマッピングなので VA_START を足せば仮想アドレスになる
			// そのアドレスを使ってページの内容をゼロクリアする
			memzero((void *)(page + VA_START), PAGE_SIZE);

			release_lock(&mm_lock);
			return page;
		}
	}

	release_lock(&mm_lock);
	PANIC("no free pages");

	return 0;
}

// 指定された仮想アドレスのぺージを解放する
void free_page(void *p){
	// 解放は単にフラグをクリアするだけ
	mem_map[(((unsigned long)p) - LOW_MEMORY) / PAGE_SIZE] = 0;
}

// ページエントリを追加する
// RPi3 では DRAM が物理アドレス 0 のところから配置されている
// つまり DRAM 上のインデックスは物理アドレスと同じ扱いになる
void map_stage2_table_entry(unsigned long *pte, unsigned long ipa, unsigned long pa, unsigned long flags) {
	// ページエントリのオフセットを index に入れる
	unsigned long index = ipa >> PAGE_SHIFT;
	index = index & (PTRS_PER_TABLE - 1);
	// エントリのオフセットを計算し書き込み
	unsigned long entry = pa | flags;
	pte[index] = entry;
}

// 該当するページテーブルのオフセットを返す(もしなければ新規に確保する)
unsigned long map_stage2_table(unsigned long *table, unsigned long shift, unsigned long ipa, int* new_table) {
	// PGD/PUD/PMD のインデックスが書かれている位置が LSB にくるようにシフト
	unsigned long index = ipa >> shift;
	// さらにインデックス部分だけを残すようにマスク
	// PGD/PUD/PMD のオフセットを指す領域はすべて9ビット(0~511)ずつなので
	// ひとつのページあたり 512 個のエントリがある (4KB / 8byte = 512)
	// (1 << 9) - 1 でその部分のマスクが作れる
	index = index & (PTRS_PER_TABLE - 1);
	if (!table[index]){
		// まだ該当インデックスにページが割当たっていない場合
		// 新規にページを確保した場合は出力変数に 1 を設定
		*new_table = 1;
		// テーブル用にページを追加
		unsigned long next_level_table = get_free_page();
		// 下位ビットにフラグを設定
		// 新たなページの用途はテーブルであって通常の領域ではないので MM_TYPE_PAGE_TABLE
		unsigned long entry = next_level_table | MM_TYPE_PAGE_TABLE;
		// エントリを追加
		table[index] = entry;
		// 返すのは下位レベルのテーブルのアドレス
		return next_level_table;
	} else {
		// 既にあるテーブルをそのまま返すときは出力変数に 0 を入れる
		*new_table = 0;
	}
	return table[index] & PAGE_MASK;
}

// vm のアドレス空間(VTTBR_EL2)のアドレス ipa に、指定されたページ page を割り当てる
// ハイパーバイザが管理するメモリマッピングは、IPA->PA のみ
void map_stage2_page(struct vm_struct *vm, unsigned long ipa, unsigned long page, unsigned long flags) {
	// 最上位のページテーブル
	unsigned long lv1_table;

	// stage2 変換用の VTTBR_EL2 に設定するテーブルを作る
	if (!vm->mm.first_table) {
		// ページテーブルがなかったら作る
		vm->mm.first_table = get_free_page();
		// 新しくページを確保したのでカウントアップする
		vm->mm.kernel_pages_count++;
	}
	lv1_table = vm->mm.first_table;

	// 新しくテーブルが追加されたかを示すフラグ
	int new_table;
	// Level 1 のテーブル(lv1_table)から対応するエントリ(lv2_table)を探す
	unsigned long lv2_table = map_stage2_table((unsigned long *)(lv1_table + VA_START), LV1_SHIFT, ipa, &new_table);
	if (new_table) {
		// もし新たにページが確保されていたらカウントアップする
		vm->mm.kernel_pages_count++;
	}
	// Level 2 のテーブル(lv2_table)から対応するエントリ(lv3_table)を探す
	unsigned long lv3_table = map_stage2_table((unsigned long *)(lv2_table + VA_START) , LV2_SHIFT, ipa, &new_table);
	if (new_table) {
		vm->mm.kernel_pages_count++;
	}
	// Level 3 のテーブル(lv3_table)の対応するエントリを探してページを登録
	map_stage2_table_entry((unsigned long *)(lv3_table + VA_START), ipa, page, flags);
	// ユーザ空間用のページ数をカウントアップする　
	vm->mm.vm_pages_count++;
}

// 指定されたゲストの仮想アドレスをゲストの物理アドレスに変換する
unsigned long get_ipa(unsigned long va) {
	// メモリページの IPA を取得
	unsigned long ipa = translate_el1(va);
	ipa &= 0xFFFFFFFFF000;
	// オフセット12ビット分を反映
	ipa |= va & 0xFFF;
	return ipa;
}

// 指定されたゲストの仮想アドレスを二段階アドレス変換しホストの物理アドレスに変換する
unsigned long get_pa_2nd(unsigned long va) {
	unsigned long pa = translate_el12(va);
	pa &= 0xFFFFFFFFF000;
	pa |= va & 0xFFF;
	return pa;
}

// ESR_EL2.ISS encoding for an exception from a Data Abort
// https://developer.arm.com/documentation/ddi0595/2021-03/AArch64-Registers/ESR-EL2--Exception-Syndrome-Register--EL2-?lang=en#fieldset_0-24_0
// SAS[23:22] Syndrome Access Size: indicates the size of the access
//            attempted by the faulting operation
//	 0b00: Byte
//	 0b01: Halfword
//	 0b10: Word
//	 0b11: Doubleword
// SRT[20:16] Syndrome Register Transfer: The register number of
//            the Wt/Wt/Rt operand of the faulting instruction
// S1PTW[7] For a stage 2 fault, indicates whether the fault was a stage 2 fault
//          on an access made for a stage 1 translation table walk:
//   0b0: Fault not on a stage 2 translation for a stage 1 translation table walk.
//   0b1: Fault on the stage 2 translation of an access for a stage 1 translation table walk.
// WnR[6] Write not Read
//   同期アボートがメモリに書いて発生したか、読み込んで発生したかを表す
//   0b0: Abort caused by an instruction reading from a memory location.
//   0b1: Abort caused by an instruction writing to a memory location.
// DFSC[5:0] Data Fault Status Code
//   0b000000: Address size fault, level 0 of translation or translation table base register.
//   0b000001: Address size fault, level 1.
//   0b000010: Address size fault, level 2.
//   0b000011: Address size fault, level 3.
//   0b000100: Translation fault, level 0.
//   0b000101: Translation fault, level 1.
//   0b000110: Translation fault, level 2.
//   0b000111: Translation fault, level 3.
//   0b001000: Access flag fault, level 0.
//   0b001001: Access flag fault, level 1.
//   0b001010: Access flag fault, level 2.
//   0b001011: Access flag fault, level 3.
//   0b001100: Permission fault, level 0.
//   0b001101: Permission fault, level 1.
//   0b001110: Permission fault, level 2.
//   0b001111: Permission fault, level 3.
//   ...
#define ISS_ABORT_DFSC_MASK		0x3f

// Translation fault: アクセスしたアドレスのエントリが invalid だった場合に発生
// Access flag fault: access flag が 0 のページテーブルエントリを
//                    TLB に読み込もうとしたときに発生

// メモリアボートが発生した場合に割込みハンドラから呼ばれる
// addr: アクセスしようとしたアドレス
// esr: exception syndrome register
// HV になっても do_mem_abort 自体の処理は変わらないが、引数として渡される値が変わっている
// ESR_EL2
// https://developer.arm.com/documentation/ddi0595/2021-03/AArch64-Registers/ESR-EL2--Exception-Syndrome-Register--EL2-?lang=en#fieldset_0-24_0
int handle_mem_abort(unsigned long addr, unsigned long esr) {
	struct vm_struct *vm = current_cpu_core()->current_vm;
	struct pt_regs *regs = vm_pt_regs(vm);
	unsigned int dfsc = esr & ISS_ABORT_DFSC_MASK;

	if (dfsc >> 2 == 0x1) {
		// ESR の3ビット目が 1 すなわち Translation fault の場合
		// つまりまだページテーブルエントリがない(invalid)ときにここにくる
		// ゲスト OS がメモリマップを追加するときに呼ばれることになる

		// ページを確保してマッピングを追加する
		unsigned long page = get_free_page();
		if (page == 0) {
			return -1;
		}
		// IPA -> PA の変換を登録
		// todo: ページ境界に合わないアドレスがくることがあるので応急処置
		addr = addr / PAGE_SIZE * PAGE_SIZE;
		map_stage2_page(vm, get_ipa(addr) & PAGE_MASK, page, MMU_STAGE2_PAGE_FLAGS);
		// INFO("VTTBR0_EL2(VMID %d): IPA 0x%lx(0x%lx in full) -> PA 0x%lx (handle_mem_abort)",
		// 	 current_cpu_core()->current_vm->vmid, get_ipa(addr) & 0xffffffffffff, addr, page);

		vm->stat.pf_trap_count++;
		return 0;
	}
	else if (dfsc >> 2 == 0x3) {
		// ESR の[3:2]ビット目が 0b11 すなわち permission fault の場合

		// 現状 vm 用のページテーブルエントリのフラグは
		// MMU_STAGE2_PAGE_FLAGS と MMU_STAGE2_MMIO_FLAGS の2種類しかない
		// 上記2つの違いは MM_STAGE2_AP_NONE と MM_STAGE2_DEVICE_MEMATTR
		// AP_NONE なのでアクセス不可で、permission fault が発生する
		// VM からは直接 MMIO 領域に触れないように
		// アクセス不可に設定してトラップできるようにしていると思われる

		const struct board_ops *ops = vm->board_ops;
		// (今のところは)アクセスサイズは 4byte 固定なので SAS は不要
		//int sas = (esr >> 22) & 0x03;	// Syndrome access size
		int srt = (esr >> 16) & 0x1f;	// Syndrome register transfer
		int wnr = (esr >>  6) & 0x01;	// Write not read
		if (wnr == 0) {
			// mmio を read しようとして例外が発生
			if (HAVE_FUNC(ops, mmio_read)) {
				// todo: なぜ IPA に変換する必要がある？
				regs->regs[srt] = ops->mmio_read(vm, get_ipa(addr));
			}
		}
		else {
			// mmio を write しようとして例外が発生
			if (HAVE_FUNC(ops, mmio_write)) {
				ops->mmio_write(vm, get_ipa(addr), regs->regs[srt]);
			}
		}

		increment_current_pc(4);
		vm->stat.mmio_trap_count++;
		return 0;
	}
	return -1;
}
