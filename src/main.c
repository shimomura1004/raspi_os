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

	irq_vector_init();
	timer_init();
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
		.filename = "TEST2.BIN",
	};
	if (create_task(raw_binary_loader, &bl_args2) < 0) {
		printf("error while starting task #2");
		return;
	}

	while (1){
		// このプロセスでは特にやることがないので CPU を明け渡す
		schedule();
	}
}
