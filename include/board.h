#ifndef _BOARD_H
#define _BOARD_H

#include "sched.h"

#define HAVE_FUNC(ops, func, ...) ((ops) && ((ops)->func))

struct board_ops {
    void (*initialize)(struct vm_struct *);
    unsigned long (*mmio_read)(struct vm_struct *, unsigned long);
    void (*mmio_write)(struct vm_struct *,unsigned long, unsigned long);
    void (*entering_vm)(struct vm_struct *);
    void (*leaving_vm)(struct vm_struct *);
    int (*is_irq_asserted)(struct vm_struct *);
    int (*is_fiq_asserted)(struct vm_struct *);
    void (*debug)(struct vm_struct *);
};

#endif
