#include <stddef.h>
#include <stdint.h>

#include "printf.h"
#include "utils.h"
#include "timer.h"
#include "irq.h"
#include "task.h"
#include "sched.h"
#include "mini_uart.h"
#include "mm.h"

// 元々(raspberry-pi-os)は
//   カーネルの仮想メモリ空間(VA)と物理メモリ(PA)がリニアマッピング(boot.S で設定)
//   ユーザプロセスの仮想メモリ空間(VA)と物理メモリ(PA)は任意のマッピング(適宜設定)
// ハイパーバイザ化により
//   ハイパーバイザの仮想メモリ空間(IPA)と物理メモリ(PA)がリニアマッピング(boot.S で設定)
//   VM の仮想メモリ空間(VA)とハイパーバイザのメモリ空間(IPA)は任意のマッピング(適宜設定)
// ここでは VM 用(EL1)のメモリマッピングを行う

// カーネル内に埋め込まれた EL1 用のコードを取り出し、EL1 用のメモリ空間にコピーする
int test_program_loader(unsigned long arg, unsigned long *pc, unsigned long *sp) {
	extern unsigned long el1_test_1;
	extern unsigned long el1_test_2;

	// el1_test_begin/el1_test_end はリンカスクリプトで指定されたアドレス
	// ユーザプログラムのコードやデータ領域の先頭と末尾のアドレスを指す
	extern unsigned long el1_test_begin;
	extern unsigned long el1_test_end;
	unsigned long begin = (unsigned long)&el1_test_begin;
	unsigned long end = (unsigned long)&el1_test_end;
	unsigned long size = end - begin;
	unsigned long func = (unsigned long)&el1_test_1;

	switch (arg)
	{
	case 1:
		func = (unsigned long)&el1_test_1;
		break;
	case 2:
		func = (unsigned long)&el1_test_2;
		break;
	}
	unsigned long entry_point = func - begin;

	// 現在実行中のタスクのページテーブルにマッピングを追加
	// current タスク用のアドレス空間にページを追加するので、仮想アドレスは任意の値でいい
	unsigned long code_page = allocate_task_page(current, 0);
	if (code_page == 0) {
		return -1;
	}
	memcpy(code_page, begin, size);

	*pc = entry_point;
	*sp = 2 * PAGE_SIZE;

	return 0;
}

// hypervisor としてのスタート地点
void hypervisor_main()
{
	uart_init();
	init_printf(NULL, putc);

	printf("=== raspvisor ===\n");

	irq_vector_init();
	timer_init();
	enable_interrupt_controller();
	enable_irq();

	// カーネルスレッドを作る
	// create_task 内で task 配列(sched.c)に要素を追加されるので
	// 呼び出すごとに新たなアドレス空間ができる 
	if (create_task(test_program_loader, 1) < 0) {
		printf("error while starting task #1");
		return;
	}

	if (create_task(test_program_loader, 2) < 0) {
		printf("error while starting task #2");
		return;
	}

	while (1){
		// このプロセスでは特にやることがないので CPU を明け渡す
		schedule();
	}
}
