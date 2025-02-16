#include "spinlock.h"
#include "sched.h"
#include "utils.h"
#include "debug.h"
#include "spinlock.h"
#include "irq.h"

extern void _spinlock_acquire(struct spinlock *);
extern void _spinlock_release(struct spinlock *);

static int holding(struct spinlock *lock) {
    return lock->locked && lock->cpuid == get_cpuid();
}

void init_lock(struct spinlock *lock, char *name) {
    lock->locked = 0;
    lock->name = name;
    lock->cpuid = -1;
}

void acquire_lock(struct spinlock *lock) {
    // todo: ロック中は割込みを禁止しておかないとデッドロックする可能性がある
// disable_irq();

    if (holding(lock)) {
        PANIC("acquire: already locked by myself");
    }

    _spinlock_acquire(&lock->locked);
    lock->cpuid = get_cpuid();
}

void release_lock(struct spinlock *lock) {
    if (!holding(lock)) {
        PANIC("release: not locked");
    }

    lock->cpuid = -1;
    _spinlock_release(&lock->locked);

// enable_irq();
}
