#include <stddef.h>
#include <stdint.h>

#include "printf.h"
#include "utils.h"
#include "timer.h"
#include "irq.h"
#include "fork.h"
#include "sched.h"
#include "mini_uart.h"
#include "sys.h"
#include "user.h"
#include "spinlock.h"
#include "debug.h"

volatile unsigned long initialized = 0;
struct spinlock log_lock;

void kernel_process(){
	printf("Kernel process started. EL %d\r\n", get_el());
	unsigned long begin = (unsigned long)&user_begin;
	unsigned long end = (unsigned long)&user_end;
	unsigned long process = (unsigned long)&user_process;
	int err = move_to_user_mode(begin, end - begin, process - begin);
	if (err < 0){
		printf("Error while moving process to user mode\n\r");
	} 
}

void kernel_main()
{
	unsigned long cpuid = get_cpuid();

	if (cpuid == 0) {
		uart_init();
		init_printf(NULL, putc);
		irq_vector_init();
		timer_init();
		enable_interrupt_controller();
		enable_irq();

		init_lock(&log_lock, "log_lock");
		INFO("Initialization complete");
		initialized = 1;
	}

	INFO("CPU %d started", cpuid);

	if (cpuid == 0) {
		int res = copy_process(PF_KTHREAD, (unsigned long)&kernel_process, 0);
		if (res < 0) {
			printf("error while starting kernel process");
			return;
		}

		while (1){
			schedule();
		}	
	}
	else {
		asm volatile("wfi");
	}
}
