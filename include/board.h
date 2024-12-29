#ifndef _BOARD_H
#define _BOARD_H

#include "sched.h"

#define HAVE_FUNC(ops, func, ...) ((ops) && ((ops)->func))

struct board_ops {
    void (*initialize)(struct task_struct *);
    unsigned long (*mmio_read)(struct task_struct *, unsigned long);
    void (*mmio_write)(struct task_struct *,unsigned long, unsigned long);
    void (*timer_tick)(struct task_struct *);
    int (*is_interrupt_required)(struct task_struct *);
};

#endif
