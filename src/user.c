#include "user_sys.h"
#include "user.h"
#include "printf.h"

void user_process()
{
	call_hvc_write("--- hvc from user process ---\r\n");
	call_hvc_exit();
}
