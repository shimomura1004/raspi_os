#include "user_sys.h"
#include "user.h"
#include "printf.h"

void user_process()
{
	call_hvc_notify();
	// for debug
	user_wfi();
	call_hvc_exit();
}
