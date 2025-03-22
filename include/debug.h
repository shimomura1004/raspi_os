#ifndef _DEBUG_H
#define _DEBUG_H

#include "printf.h"
#include "entry.h"
#include "sched.h"
#include "utils.h"
#include "spinlock.h"
#include "irq.h"
#include "cpu_core.h"

extern struct spinlock log_lock;

#define _LOG_COMMON(level, fmt, ...) do { \
    acquire_lock(&log_lock); \
    unsigned long cpuid = get_cpuid(); \
    struct vm_struct *vm = current_cpu_core()->current_vm; \
    if (vm) { \
        printf("<cpu:%d>[vmid:%d] %s: ", cpuid, vm->vmid, level); \
    } \
    else { \
        printf("<cpu:%d> %s: ", cpuid, level); \
    } \
    printf(fmt "\n", ##__VA_ARGS__); \
    release_lock(&log_lock); \
} while (0)

#define INFO(fmt, ...) _LOG_COMMON("INFO", fmt, ##__VA_ARGS__)
#define WARN(fmt, ...) _LOG_COMMON("WARN", fmt, ##__VA_ARGS__)

// panic 後も割り込みが入ると普通に動いてしまうので割り込みを禁止する
#define PANIC(fmt, ...) do { \
    _LOG_COMMON("PANIC", fmt, ##__VA_ARGS__); \
    struct vm_struct *vm = current_cpu_core()->current_vm; \
    if (vm) { \
        exit_vm(); \
    } \
    else { \
        disable_irq(); \
        err_hang(); \
    } \
} while(0)

#endif
