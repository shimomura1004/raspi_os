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


// // カーネルスレッドを立ち上げ、ユーザプロセスに移行させる
// void kernel_process(){
// 	printf("Kernel process started. EL %d\r\n", get_el());
// 	// user_begin はリンカスクリプトで設定されるアドレスで、ユーザ空間用のテキスト領域の先頭
// 	unsigned long begin = (unsigned long)&user_begin;
// 	unsigned long end = (unsigned long)&user_end;
// 	unsigned long process = (unsigned long)&user_process;

// 	// ここで自分自身をカーネル空間(EL1)からユーザ空間(EL0)に移す
// 	int err = move_to_user_mode(begin, end - begin, process - begin);
// 	if (err < 0){
// 		printf("Error while moving process to user mode\n\r");
// 	}
// }

// HLOS としてのスタート地点
void kernel_main()
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
