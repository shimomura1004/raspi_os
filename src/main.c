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
	// entry_point(ロード先)は EL1 で使う仮想アドレスなのでどこでもいい
	// stage 1 アドレス変換で entry_point(VA) は IPA に変換される
	// stage 2 はリニアマッピングなので、IPA の値がそのまま PA になる
	const unsigned long entry_point = 0x80000;

	// el1_test_begin/el1_test_end はリンカスクリプトで指定されたアドレス
	// ユーザプログラムのコードやデータ領域の先頭と末尾
	extern unsigned long el1_test_begin;
	extern unsigned long el1_test_end;
	unsigned long begin = (unsigned long)&el1_test_begin;
	unsigned long end = (unsigned long)&el1_test_end;
	unsigned long size = end - begin;

	// 現在実行中のタスクのページテーブルにマッピングを追加
	unsigned long code_page = allocate_user_page(current, entry_point);
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

	printf("Starting hypervisor (EL %d)...\r\n", get_el());

	irq_vector_init();
	timer_init();
	enable_interrupt_controller();
	enable_irq();

	// カーネルスレッドを作る(EL2 で動く)
	int res = create_task(test_program_loader, 0);
	if (res < 0) {
		printf("error while starting kernel process");
		return;
	}

	printf("Starting process...\r\n");

	while (1){
		// このプロセスでは特にやることがないので CPU を明け渡す
		schedule();
	}
}
