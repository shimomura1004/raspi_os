#ifndef _BOARD_H
#define _BOARD_H

#include "sched.h"

#define HAVE_FUNC(ops, func, ...) ((ops) && ((ops)->func))

struct board_ops {
    void (*initialize)(struct vcpu_struct *);
    unsigned long (*mmio_read)(struct vcpu_struct *, unsigned long);
    void (*mmio_write)(struct vcpu_struct *,unsigned long, unsigned long);
    void (*entering_vm)(struct vcpu_struct *);
    void (*leaving_vm)(struct vcpu_struct *);
    int (*is_irq_asserted)(struct vcpu_struct *);
    int (*is_fiq_asserted)(struct vcpu_struct *);
    void (*debug)(struct vcpu_struct *);
};

#endif
