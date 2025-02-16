#ifndef _DEBUG_H
#define _DEBUG_H

#include "printf.h"
#include "entry.h"

#include "spinlock.h"
extern struct spinlock log_lock;

#define _LOG_COMMON(level, fmt, ...) do { \
    spinlock_acquire(&log_lock); \
    if (current) { \
        printf("<%d>%s[%d]: ", get_cpuid(), level, current->pid); \
    } \
    else { \
        printf("%s[?]: ", level); \
    } \
    printf(fmt "\n", ##__VA_ARGS__); \
    spinlock_release(&log_lock); \
} while (0)

#define INFO(fmt, ...) _LOG_COMMON("INFO", fmt, ##__VA_ARGS__)
#define WARN(fmt, ...) _LOG_COMMON("WARN", fmt, ##__VA_ARGS__)

#define PANIC(fmt, ...) do { \
    _LOG_COMMON("PANIC", fmt, ##__VA_ARGS__); \
    if (current) { \
        exit_task(); \
    } \
    else { \
        err_hang(); \
    } \
} while(0)

#endif
