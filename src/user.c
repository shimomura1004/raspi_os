#include "user_sys.h"
#include "user.h"
#include "printf.h"

void user_process()
{
	call_hvc_notify();
	call_hvc_exit();
}
