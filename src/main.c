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

	irq_vector_init();
	timer_init();
	enable_interrupt_controller();
	enable_irq();

	if (sd_init() < 0) {
		PANIC("sd_init() failed");
	}

	// img には entrypoint などの情報がないので自分で入れる必要がある
	struct raw_binary_loader_args bl_args = {
		.loader_addr = 0x0,
		.entry_point = 0x0,
		.sp = 0x4000,
		.filename = "test.bin",
	};

	if (create_task(raw_binary_loader, &bl_args) < 0) {
		printf("error while starting task #1");
	}

	while (1){
		// このプロセスでは特にやることがないので CPU を明け渡す
		schedule();
	}
}
