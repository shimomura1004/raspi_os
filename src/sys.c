#include "task.h"
#include "printf.h"
#include "utils.h"
#include "sched.h"
#include "mm.h"

// システムコールの実体

void sys_notify(void){
	printf("HVC!\n");
}

void sys_exit(){
	exit_process();
}

void * const hvc_table[] = {sys_notify, sys_exit};
