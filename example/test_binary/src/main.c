#include "../../../include/loader.h"

char *hello_str = "hello world";

struct raw_binary_loader_args echo_bin_args = {
	.loader_addr = 0x0,
	.entry_point = 0x0,
	.sp = 0x100000,
	.filename = "ECHO.ELF",
};
struct raw_binary_loader_args *echo_bin_args_p = &echo_bin_args;

int main() {
  return 0;
}
