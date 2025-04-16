#ifndef _SPINLOCK_H
#define _SPINLOCK_H

struct spinlock {
    unsigned long locked;
    char *name;
    // todo: もう少し情報を追加して構造体にする
    long cpuid;
};

void init_lock(struct spinlock *lock, char *name);
void acquire_lock(struct spinlock *lock);
void release_lock(struct spinlock *lock);

void push_disable_irq();
void pop_disable_irq();

#endif
