#ifndef _LOADER_H
#define _LOADER_H

#define MAX_FILE_PATH 256

struct loader_args {
    unsigned long loader_addr;
    unsigned long entry_point;
    unsigned long sp;
    char filename[MAX_FILE_PATH];
};

int elf_binary_loader(void *, unsigned long *, unsigned long *);
int raw_binary_loader(void *, unsigned long *, unsigned long *);

struct vm_struct;
void copy_code_to_memory(struct vm_struct *vm, unsigned long va, unsigned long from, unsigned long size);
int load_file_to_memory(struct vm_struct *tsk, const char *name, unsigned long va);

#endif
