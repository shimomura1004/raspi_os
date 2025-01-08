#ifndef _LOADER_H
#define _LOADER_H

#include "sched.h"

int load_file_to_memory(struct task_struct *tsk, const char *name, unsigned long va);

#endif
