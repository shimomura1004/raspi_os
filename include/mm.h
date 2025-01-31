#ifndef	_MM_H
#define	_MM_H

#include "peripherals/base.h"

#define VA_START 			0x0000000000000000

#define PHYS_MEMORY_SIZE 		0x40000000

#define PAGE_MASK			0xfffffffffffff000
#define PAGE_SHIFT	 		12
#define TABLE_SHIFT 			9
#define SECTION_SHIFT			(PAGE_SHIFT + TABLE_SHIFT)

#define PAGE_SIZE   			(1 << PAGE_SHIFT)
#define SECTION_SIZE			(1 << SECTION_SHIFT)

// メモリのうち最初の 4MB はカーネルイメージと init task のスタック用
// なので low_memory は 4MB(2 x section size)のところを指す
#define LOW_MEMORY              	(2 * SECTION_SIZE)
// メモリの末尾の 1MB はデバイスレジスタ用(device_base)なので
// high memory としては device base となる
#define HIGH_MEMORY             	DEVICE_BASE

// low_memory~high_memory が自由に使えるメモリ
// ページングの対象になるメモリのサイズ
#define PAGING_MEMORY 			(HIGH_MEMORY - LOW_MEMORY)
// 含まれるページの数
#define PAGING_PAGES 			(PAGING_MEMORY/PAGE_SIZE)

#define PTRS_PER_TABLE			(1 << TABLE_SHIFT)

// ハイパーバイザ化により、今まで stage1 で使っていたテーブルは stage2 として使われる
// for 2 translation (IPA to PA)
#define PGD_SHIFT			(PAGE_SHIFT + 3 * TABLE_SHIFT)
#define PUD_SHIFT			(PAGE_SHIFT + 2 * TABLE_SHIFT)
#define PMD_SHIFT			(PAGE_SHIFT +     TABLE_SHIFT)

// for stage 2 translation (VA to IPA)
// https://developer.arm.com/documentation/102142/0100/Stage-2-translation
// for armv7
//   https://developer.arm.com/documentation/den0013/d/The-Memory-Management-Unit/Level-2-translation-tables
#define LV1_SHIFT           (PAGE_SHIFT + 2 * TABLE_SHIFT)
#define LV2_SHIFT           (PAGE_SHIFT +     TABLE_SHIFT)

#define PG_DIR_SIZE			(3 * PAGE_SIZE)

#ifndef __ASSEMBLER__

#include "sched.h"

unsigned long get_free_page();
void free_page(void *p);
void map_stage2_page(struct task_struct *task, unsigned long va,
                     unsigned long page, unsigned long flags);
unsigned long allocate_page();
unsigned long allocate_task_page(struct task_struct *task, unsigned long va);
void set_task_page_notaccessable(struct task_struct *task, unsigned long va);

int handle_mem_abort(unsigned long addr, unsigned long esr);

unsigned long get_ipa(unsigned long va);
extern unsigned long pg_dir;

#endif

#endif  /*_MM_H */
