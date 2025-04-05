#include "printf.h"
#include "utils.h"
#include "mini_uart.h"
#include "../../../include/loader.h"

#define BUFFER_LENGTH 128

void new_vm();

struct loader_args vm_args = {
	.loader_addr = 0x0,
	.entry_point = 0x0,
	.sp = 0x100000,
	.filename = "",
};
struct loader_args *vm_args_p = &vm_args;

int strncmp(char *a, char *b, int n) {
	for (int i=0; i < n; i++) {
		if (a[i] != b[i]) {
			return 1;
		}
	}
	return 0;
}

#define EQUAL(A, B) (strncmp(A, B, sizeof(B)) == 0)
void execute_command(char *buf) {
	char *command = buf;
	char *arg = 0;

	for (int i=0; i < BUFFER_LENGTH; i++) {
		if (buf[i] == ' ') {
			arg = &buf[i + 1];
			buf[i] = 0;
			break;
		}
	}

	if (arg == 0) {
		printf("error: %s\n", buf);
		return;
	}

	if (EQUAL(command, "new")) {
		printf("create a new vm: %s\n", arg);
		for (int i=0; (vm_args.filename[i] = arg[i]); i++);
		new_vm();
	}
	else if (EQUAL(command, "kill")) {
	}
	else if (EQUAL(command, "shutdown")) {
	}
	else {
		printf("command error: %s\n", command);
	}
}

void kernel_main(void)
{
	uart_init();
	init_printf(0, putc);

	int count = 0;
	char buf[BUFFER_LENGTH];

	while (1) {
		count = 0;
		printf("> ");

		while (count < BUFFER_LENGTH) {
			buf[count] = uart_recv();

			if (buf[count] == '\n' || buf[count] == '\r') {
				printf("\n");

				buf[count] = 0;
				execute_command(buf);
				break;
			}

			uart_send(buf[count]);
			count++;
		}
	}
}
