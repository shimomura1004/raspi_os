#include "spinlock.h"
#include "sched.h"
#include "utils.h"
#include "debug.h"
#include "spinlock.h"
#include "irq.h"

// todo: 正しい位置に
#define NUMBER_OF_CPUS 4
struct cpu_struct cpus[NUMBER_OF_CPUS];
void init_cpus() {
	for (int i = 0; i < NUMBER_OF_CPUS; i++) {
		cpus[i].current = NULL;
		cpus[i].number_of_off = 0;
		cpus[i].interrupt_enable = 0;
	}
}

struct cpu_struct *current_cpu() {
    unsigned long cpuid = get_cpuid();
    return cpus + cpuid;
//	return &cpus[get_cpuid()];
}

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
push_disable_irq();

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

pop_disable_irq();
}

// 多重で CPU 割込みを禁止するときに使う、割込み禁止関数
// push された数と同じだけ pop しないと割込みが有効にならない
void push_disable_irq() {
    // todo: interrupt_enable は必要ないのでは
    // int old = is_interrupt_enabled();
    disable_irq();

    struct cpu_struct *cpu = current_cpu();
    if (cpu->number_of_off == 0) {
        // cpu->interrupt_enable = old;
    }
    cpu->number_of_off++;
}

void pop_disable_irq() {
    struct cpu_struct *cpu = current_cpu();
    if (is_interrupt_enabled()) {
        PANIC("interruptible");
    }
    if (cpu->number_of_off <= 0) {
        PANIC("number_of_off is 0");
    }

    cpu->number_of_off--;
    if (cpu->number_of_off == 0) {
        // todo: ここでクラッシュしている？
        // 片方のコアが文字列を出力中なのに、もう片方のコアが enable しようとしてる
        enable_irq();
    }
}
