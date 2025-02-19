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
#include "sd.h"
#include "debug.h"
#include "loader.h"

// todo: 他の種類の OS のロード
// この情報は、あとから VM にコンテキストスイッチしたときに参照される
// そのときまで解放されないようにグローバル変数としておく
// img には entrypoint などの情報がないので自分で入れる必要がある
static struct raw_binary_loader_args echo_bin_args = {
	.loader_addr = 0x0,
	.entry_point = 0x0,
	.sp = 0x100000,
	.filename = "ECHO.BIN",
};
struct raw_binary_loader_args mini_os_bin_args = {
	.loader_addr = 0x0,
	.entry_point = 0x0,
	.sp = 0x100000,
	.filename = "MINI-OS.BIN",
};
struct raw_binary_loader_args mini_os_elf_args = {
	.loader_addr = 0x0,
	.entry_point = 0x0,
	.sp = 0xffff000000100000,
	.filename = "MINI-OS.ELF",
};
struct raw_binary_loader_args echo_elf_args = {
	.loader_addr = 0x0,
	.entry_point = 0x0,
	.sp = 0x100000,
	.filename = "ECHO.ELF",
};
struct raw_binary_loader_args test_bin_args = {
	.loader_addr = 0x0,
	.entry_point = 0x0,
	.sp = 0x100000,
	.filename = "TEST2.BIN",
};

static void initialize_hypervisor() {
	uart_init();
	init_printf(NULL, putc);

	printf("=== raspvisor ===\n");

	// ホスト用のコンソールの初期化
	init_task_console(current);

	init_initial_task();

	irq_vector_init();
	timer_init();

	// 中途半端なところで割込み発生しないようにタイマと UART の有効化が終わるまで割込み禁止
	disable_irq();
	enable_interrupt_controller();
	enable_irq();

	if (sd_init() < 0) {
		PANIC("sd_init() failed");
	}
}

static void load_guest_oss() {
	if (create_task(raw_binary_loader, &echo_bin_args) < 0) {
		printf("error while starting task #1");
	}

	if (create_task(raw_binary_loader, &mini_os_bin_args) < 0) {
		printf("error while starting task #2");
	}

	// if (create_task(elf_binary_loader, &mini_os_elf_args) < 0) {
	// 	printf("error while starting task #3");
	// }

	// if (create_task(elf_binary_loader, &echo_elf_args) < 0) {
	// 	printf("error while starting task #4");
	// }

	// if (create_task(raw_binary_loader, &test_bin_args) < 0) {
	// 	printf("error while starting task #5");
	// }
}

// hypervisor としてのスタート地点
// todo: マルチコアで実行し、複数コアで文字出力するとクラッシュする
void hypervisor_main()
{
	// ハイパーバイザの初期化とゲストのロードを実施
	initialize_hypervisor();
	load_guest_oss();
	
	while (1){
		// todo: schedule を呼ぶ前に手動で割込みを禁止にしないといけないのは危ない
		disable_irq();
		// このプロセスでは特にやることがないので CPU を明け渡す
		// todo: ゲスト OS のコントロールができるといい
		schedule();
		enable_irq();
	}
}
