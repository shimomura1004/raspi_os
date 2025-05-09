#include "spinlock.h"
#include "sched.h"
#include "utils.h"
#include "debug.h"
#include "spinlock.h"
#include "irq.h"
#include "cpu_core.h"

// todo: sleeplock も実装したい
//   sleeplock はスケジューラを使って実装する
//   他プロセスとロックが重複したら、そのロックの ID を保持しつつプロセスを SLEEP させる
//   他プロセスがロックを解放したら、そのロックの ID で待っているプロセス達を一斉に RUNNABLE にする
//     1つのプロセスだけが sleeplock を取るために spinlock で守る必要あり
//   最初にロックを取れたプロセス以外は再び SLEEP しないといけないので、sleeplock 内でループさせる

extern void _spinlock_acquire(unsigned long *);
extern void _spinlock_release(unsigned long *);

static int holding(struct spinlock *lock) {
    return lock->locked && lock->cpuid == get_cpuid();
}

// 多重で CPU 割込みを禁止するときに使う、割込み禁止関数
// push された数と同じだけ pop しないと割込みが有効にならない
void push_disable_irq() {
    int old = is_interrupt_enabled();
    disable_irq();

    struct cpu_core_struct *cpu = current_cpu_core();
    if (cpu->number_of_off == 0) {
        cpu->interrupt_enable = old;
    }
    cpu->number_of_off++;
}

void pop_disable_irq() {
    struct cpu_core_struct *cpu = current_cpu_core();

    if (is_interrupt_enabled()) {
        PANIC("interruptible");
    }
    if (cpu->number_of_off <= 0) {
        PANIC("number_of_off is 0");
    }

    cpu->number_of_off--;
    if (cpu->number_of_off == 0 && cpu->interrupt_enable) {
        enable_irq();
    }
}

void init_lock(struct spinlock *lock, char *name) {
    lock->locked = 0;
    lock->name = name;
    lock->cpuid = -1;
}

void acquire_lock(struct spinlock *lock) {
    // ロック中は割込みを禁止しておかないとデッドロックする可能性がある
    push_disable_irq();

    unsigned long cpuid = get_cpuid();
    if (holding(lock)) {
        PANIC("acquire: already locked by myself(cpu: %d)", cpuid);
    }

    // サポートされるなら __sync_lock_test_and_set を使うほうがいい
    _spinlock_acquire(&lock->locked);
    lock->cpuid = cpuid;
}

void release_lock(struct spinlock *lock) {
    if (!holding(lock)) {
        PANIC("release: not locked");
    }

    lock->cpuid = -1;
    _spinlock_release(&lock->locked);

    pop_disable_irq();
}
