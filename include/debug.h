#ifndef _DEBUG_H
#define _DEBUG_H

#include "printf.h"
#include "entry.h"
#include "sched.h"
#include "utils.h"
#include "spinlock.h"
#include "irq.h"
#include "cpu_core.h"

// ログレベルの定義
#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_PANIC 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4

// デフォルトのログレベル（コンパイル時に上書き可能）
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO  // デフォルトは警告まで表示
#endif

// todo: debug.c 的なソースを用意して隠蔽する
extern struct spinlock log_lock;

#define _LOG_COMMON(level, fmt, ...) do { \
    acquire_lock(&log_lock); \
    unsigned long cpuid = get_cpuid(); \
    struct vcpu_struct *vm = current_cpu_core()->current_vcpu; \
    if (vm) { \
        printf("<cpu:%d>[vmid:%d] %s: ", cpuid, vm->vmid, level); \
    } \
    else { \
        printf("<cpu:%d> %s: ", cpuid, level); \
    } \
    printf(fmt "\n", ##__VA_ARGS__); \
    release_lock(&log_lock); \
} while (0)

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define DEBUG(fmt, ...) _LOG_COMMON("\x1b[39m" "DEBUG" "\x1b[39m", fmt, ##__VA_ARGS__)
#else
#define DEBUG(fmt, ...) do {} while(0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define INFO(fmt, ...)  _LOG_COMMON("\x1b[36m" "INFO" "\x1b[39m", fmt, ##__VA_ARGS__)
#else
#define DEBUG(fmt, ...) do {} while(0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#define WARN(fmt, ...)  _LOG_COMMON("\x1b[33m" "WARN" "\x1b[39m", fmt, ##__VA_ARGS__)
#else
#define DEBUG(fmt, ...) do {} while(0)
#endif

// panic 後も割り込みが入ると普通に動いてしまうので割り込みを禁止する
#define PANIC(fmt, ...) do { \
    _LOG_COMMON("PANIC", fmt, ##__VA_ARGS__); \
    struct vcpu_struct *vm = current_cpu_core()->current_vcpu; \
    if (vm) { \
        exit_vm(); \
    } \
    else { \
        disable_irq(); \
        err_hang(); \
    } \
} while(0)

#endif
