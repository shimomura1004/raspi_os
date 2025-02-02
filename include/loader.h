#ifndef _LOADER_H
#define _LOADER_H

#include "sched.h"

struct raw_binary_loader_args {
    unsigned long loader_addr;
    unsigned long entry_point;
    unsigned long sp;
    const char *filename;
};

int elf_binary_loader(void *, unsigned long *, unsigned long *);
int raw_binary_loader(void *, unsigned long *, unsigned long *);
int test_program_loader(void *, unsigned long *, unsigned long *);

int load_file_to_memory(struct task_struct *tsk, const char *name, unsigned long va);

#endif
