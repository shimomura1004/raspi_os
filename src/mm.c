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
	map_page(task, va, page);
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
void map_table_entry(unsigned long *pte, unsigned long va, unsigned long pa) {
	// ページエントリのオフセットを index に入れる
	unsigned long index = va >> PAGE_SHIFT;
	index = index & (PTRS_PER_TABLE - 1);
	// エントリのオフセットを計算し書き込み
	unsigned long entry = pa | MMU_PTE_FLAGS; 
	pte[index] = entry;
}

// 該当するページテーブルのオフセットを返す(もしなければ新規に確保する)
unsigned long map_table(unsigned long *table, unsigned long shift, unsigned long va, int* new_table) {
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
void map_page(struct task_struct *task, unsigned long va, unsigned long page){
	// page global directory
	unsigned long pgd;

	if (!task->mm.pgd) {
		// Page global directory がなかったら、まずそれを作る
		task->mm.pgd = get_free_page();
		// 今確保したページをカーネルの管理対象に加える
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = task->mm.pgd;
	}
	pgd = task->mm.pgd;

	// 新しくテーブルが追加されたかを示すフラグ
	// RPi OS では PGD/PUD/PMD は1個ずつしかないので初回しか追加されないはず
	int new_table;
	// PGD
	unsigned long pud = map_table((unsigned long *)(pgd + VA_START), PGD_SHIFT, va, &new_table);
	if (new_table) {
		// もし新たにページが確保されていたら使用ページを登録
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = pud;
	}
	// PUD
	unsigned long pmd = map_table((unsigned long *)(pud + VA_START) , PUD_SHIFT, va, &new_table);
	if (new_table) {
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = pmd;
	}
	// PMD
	unsigned long pte = map_table((unsigned long *)(pmd + VA_START), PMD_SHIFT, va, &new_table);
	if (new_table) {
		task->mm.kernel_pages[++task->mm.kernel_pages_count] = pte;
	}

	// 最後にページエントリの追加
	map_table_entry((unsigned long *)(pte + VA_START), va, page);
	struct user_page p = {page, va};
	// ユーザ空間の管理対象に加える
	task->mm.user_pages[task->mm.user_pages_count++] = p;
}

// 仮想アドレス空間のコピー
int copy_virt_memory(struct task_struct *dst) {
	struct task_struct* src = current;
	// ユーザ空間にマップしてあるページを順番にコピー
	for (int i = 0; i < src->mm.user_pages_count; i++) {
		// 新しくページを確保して
		unsigned long kernel_va = allocate_user_page(dst, src->mm.user_pages[i].virt_addr);
		if( kernel_va == 0) {
			return -1;
		}
		// ページを丸ごとコピー
		memcpy(kernel_va, src->mm.user_pages[i].virt_addr, PAGE_SIZE);
	}
	return 0;
}

// todo: これはなに？
static int ind = 1;

// メモリアボートが発生した場合に割込みハンドラから呼ばれる
// addr: アクセスしようとしたアドレス
// esr: exception syndrome register
int do_mem_abort(unsigned long addr, unsigned long esr) {
	// メモリアボートは、アクセス権限のエラーなどでも発生する
	// ページテーブルが未割当の場合のみ処理したいので esr の値を見て分岐する
	unsigned long dfs = (esr & 0b111111);
	if ((dfs & 0b111100) == 0b100) {
		unsigned long page = get_free_page();
		if (page == 0) {
			return -1;
		}
		map_page(current, addr & PAGE_MASK, page);
		ind++;
		if (ind > 2){
			return -1;
		}
		return 0;
	}
	return -1;
}
