#include <stddef.h>
#include <stdint.h>

#include "printf.h"
#include "utils.h"
#include "timer.h"
#include "irq.h"
#include "task.h"
#include "sched.h"
#include "mini_uart.h"
#include "sys.h"
#include "user.h"

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
	int res = create_vmtask(0);
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
