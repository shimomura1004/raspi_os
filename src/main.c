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

// hypervisor としてのスタート地点
void hypervisor_main()
{
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

	// img には entrypoint などの情報がないので自分で入れる必要がある
	struct raw_binary_loader_args bl_args1 = {
		.loader_addr = 0x0,
		.entry_point = 0x0,
		.sp = 0x100000,
		.filename = "ECHO.BIN",
	};
	if (create_task(raw_binary_loader, &bl_args1) < 0) {
		printf("error while starting task #1");
		return;
	}

	struct raw_binary_loader_args bl_args2 = {
		.loader_addr = 0x0,
		.entry_point = 0x0,
		.sp = 0x100000,
		.filename = "MINI-OS.BIN",
	};
	if (create_task(raw_binary_loader, &bl_args2) < 0) {
		printf("error while starting task #2");
		return;
	}

	struct raw_binary_loader_args bl_args3 = {
		.loader_addr = 0x0,
		.entry_point = 0x0,
		.sp = 0xffff000000100000,
		.filename = "MINI-OS.ELF",
	};
	if (create_task(elf_binary_loader, &bl_args3) < 0) {
		printf("error while starting task #3");
		return;
	}

	struct raw_binary_loader_args bl_args4 = {
		.loader_addr = 0x0,
		.entry_point = 0x0,
		.sp = 0x100000,
		.filename = "ECHO.ELF",
	};
	if (create_task(elf_binary_loader, &bl_args4) < 0) {
		printf("error while starting task #4");
		return;
	}

	struct raw_binary_loader_args bl_args5 = {
		.loader_addr = 0x0,
		.entry_point = 0x0,
		.sp = 0x100000,
		.filename = "MINI-OS.BIN",
	};
	if (create_task(raw_binary_loader, &bl_args5) < 0) {
		printf("error while starting task #5");
		return;
	}

	while (1){
		// todo: schedule を呼ぶ前に手動で割込みを禁止にしないといけないのは危ない
		disable_irq();
		// このプロセスでは特にやることがないので CPU を明け渡す
		schedule();
		enable_irq();
	}
}
