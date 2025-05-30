#include "printf.h"
#include "utils.h"
#include "mini_uart.h"

void kernel_main(void)
{
	uart_init();
	init_printf(0, putc);
	int el = get_el();
	printf("Exception level: %d \r\n", el);

	while (1) {
		char c = uart_recv();
		c = c == '\r' ? '\n' : c;
		uart_send(c);
	}
}
