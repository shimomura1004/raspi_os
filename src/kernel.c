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


// カーネルスレッドを立ち上げ、ユーザプロセスに移行させる
void kernel_process(){
	printf("Kernel process started. EL %d\r\n", get_el());
	// user_begin はリンカスクリプトで設定されるアドレスで、ユーザ空間用のテキスト領域の先頭
	unsigned long begin = (unsigned long)&user_begin;
	unsigned long end = (unsigned long)&user_end;
	unsigned long process = (unsigned long)&user_process;
	// プロセスをユーザ空間に移す
	int err = move_to_user_mode(begin, end - begin, process - begin);
	if (err < 0){
		printf("Error while moving process to user mode\n\r");
	} 
}

// HLOS としてのスタート地点
void kernel_main()
{
	uart_init();
	init_printf(NULL, putc);
	irq_vector_init();
	timer_init();
	enable_interrupt_controller();
	enable_irq();

	// カーネルスレッドを作る
	int res = copy_process(PF_KTHREAD, (unsigned long)&kernel_process, 0);
	if (res < 0) {
		printf("error while starting kernel process");
		return;
	}

	while (1){
		schedule();
	}	
}
