#include "mm.h"
#include "arm/mmu.h"

// ページの使用状況を表す領域
static unsigned short mem_map [ PAGING_PAGES ] = {0,};

// カーネル空間で使うためのページを確保し、その仮想アドレスを返す
// RPi OS では "カーネルのアドレス空間" はないのでマッピングの追加は行わない
unsigned long allocate_kernel_page() {
	// 未使用ページを探す
	unsigned long page = get_free_page();
	if (page == 0) {
		return 0;
	}
	// RPi OS はリニアマッピング
	// 単純に仮想アドレス空間の開始アドレスをオフセットとして足すと仮想アドレスになる
	return page + VA_START;
}

// ユーザ空間で使うためのページを確保し、その仮想アドレスを返す
unsigned long allocate_user_page(struct task_struct *task, unsigned long va) {
	// 未使用ページを探す、page は仮想アドレスではなくオフセット
	unsigned long page = get_free_page();
	if (page == 0) {
		return 0;
	}
	// 新たに確保したページをこのタスクのアドレス空間にマッピングする
	map_stage2_page(task, va, page);
	// 新たに確保したページの仮想アドレスを返す(リニアマッピングなのでオフセットを足すだけ)
	return page + VA_START;
}

// 未使用のページを探してその場所(DRAM 内のオフセット)を返す
unsigned long get_free_page()
{
	for (int i = 0; i < PAGING_PAGES; i++){
		if (mem_map[i] == 0){
			// 未使用領域を見つけたらフラグを立てる
			mem_map[i] = 1;
			unsigned long page = LOW_MEMORY + i*PAGE_SIZE;
			// RPi OS はリニアマッピングなので VA_START を足せば仮想アドレスになる
			// そのアドレスを使ってページの内容をゼロクリアする
			memzero(page + VA_START, PAGE_SIZE);
			return page;
		}
	}
	return 0;
}

// 指定された仮想アドレスのぺージを解放する
void free_page(unsigned long p){
	// 解放は単にフラグをクリアするだけ
	mem_map[(p - LOW_MEMORY) / PAGE_SIZE] = 0;
}

// ページエントリを追加する
// RPi3 では DRAM が物理アドレス 0 のところから配置されている
// つまり DRAM 上のインデックスは物理アドレスと同じ扱いになる
void map_stage2_table_entry(unsigned long *pte, unsigned long va, unsigned long pa) {
	// ページエントリのオフセットを index に入れる
	unsigned long index = va >> PAGE_SHIFT;
	index = index & (PTRS_PER_TABLE - 1);
	// エントリのオフセットを計算し書き込み
	unsigned long entry = pa | MMU_STAGE2_PAGE_FLAGS;
	pte[index] = entry;
}

// 該当するページテーブルのオフセットを返す(もしなければ新規に確保する)
unsigned long map_stage2_table(unsigned long *table, unsigned long shift, unsigned long va, int* new_table) {
	// PGD/PUD/PMD のインデックスが書かれている位置が LSB にくるようにシフト
	unsigned long index = va >> shift;
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

// task のアドレス空間のアドレス va に、指定されたページ page を割り当てる
// stage1/2 のどちらも差はない
void map_stage2_page(struct task_struct *task, unsigned long va, unsigned long page){
	// 最上位のページテーブル
	unsigned long lv1_table;

	if (!task->mm.first_table) {
		// ページテーブルがなかったら作る
		task->mm.first_table = get_free_page();
		// 今確保したページをカーネルの管理対象に加える
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = task->mm.first_table;
	}
	lv1_table = task->mm.first_table;

	// 新しくテーブルが追加されたかを示すフラグ
	int new_table;
	// Level 1 のテーブル(lv1_table)から対応するエントリ(lv2_table)を探す
	unsigned long lv2_table = map_stage2_table((unsigned long *)(lv1_table + VA_START), LV1_SHIFT, va, &new_table);
	if (new_table) {
		// もし新たにページが確保されていたら使用ページを登録
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = lv2_table;
	}
	// Level 2 のテーブル(lv2_table)から対応するエントリ(lv3_table)を探す
	unsigned long lv3_table = map_stage2_table((unsigned long *)(lv2_table + VA_START) , LV2_SHIFT, va, &new_table);
	if (new_table) {
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = lv3_table;
	}
	// Level 3 のテーブル(lv3_table)の対応するエントリを探してページを登録
	map_stage2_table_entry((unsigned long *)(lv3_table + VA_START), va, page);
	struct user_page p = {page, va};
	// ユーザ空間の管理対象に加える
	task->mm.user_pages[task->mm.user_pages_count++] = p;
}

#define ISS_ABORT_S1PTW			(1<<7)

#define ISS_ABORT_IFSC			0b111111
#define IFSC_TRANS_FAULT_EL1	0b000101

// メモリアボートが発生した場合に割込みハンドラから呼ばれる
// addr: アクセスしようとしたアドレス
// esr: exception syndrome register
// HV になっても do_mem_abort 自体の処理は変わらないが、引数として渡される値が変わっている
int do_mem_abort(unsigned long addr, unsigned long esr) {
	// メモリアボートは、アクセス権限のエラーなどでも発生する
	// ページテーブルが未割当の場合のみ処理したいので esr の値を見て分岐する
	if ((esr & ISS_ABORT_S1PTW) == ISS_ABORT_S1PTW) {
		// stage 2 translation fault
		unsigned long page = get_free_page();
		if (page == 0) {
			return -1;
		}
		map_stage2_page(current, addr & PAGE_MASK, page);
		return 0;
	}
	return -1;
}
