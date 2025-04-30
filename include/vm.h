#ifndef _TASK_H
#define _TASK_H

#include "sched.h"

/*
 * PSR(Program Status Register) bits
 */
#define PSR_MODE_EL0t 0x00000000	// 0b0000: EL0
#define PSR_MODE_EL1t 0x00000004	// 0b0100: EL1 with SP_EL0 (EL1t)
#define PSR_MODE_EL1h 0x00000005	// 0b0101: EL1 with SP_EL1 (EL1h)
#define PSR_MODE_EL2t 0x00000008	// 0b1000: EL2 with SP_EL0 (EL2t)
#define PSR_MODE_EL2h 0x00000009	// 0b1001: EL2 with SP_EL2 (EL2h)
#define PSR_MODE_EL3t 0x0000000c	// 0b1100: EL3 with SP_EL0 (EL3t)
#define PSR_MODE_EL3h 0x0000000d	// 0b1101: EL3 with SP_EL3 (EL3h)

// 第二、第三引数はどちらも出力引数
typedef int (*loader_func_t)(void *, unsigned long *, unsigned long *);
struct pt_regs *vm_pt_regs(struct vcpu_struct *);

int create_idle_vm(unsigned long cpuid);
int create_vm_with_loader(loader_func_t, void *);

int is_uart_forwarded_vm(struct vcpu_struct *);
void flush_vm_console(struct vcpu_struct *);
void increment_current_pc(int);

// PSTATE
// https://developer.arm.com/documentation/102412/0103/Handling-exceptions/Taking-an-exception?lang=en#md244-taking-an-exception__saving-the-current-processor-state
// PSTATE は SPSR に保存される、たとえば EL0 で例外発生すると SPSR_EL1 に保存される
//
// AArch64 has a concept of processor state known as PSTATE, it is this information that is stored in the SPSR.
// PSTATE contains things like current Exception level and ALU flags. In AArch64, this includes:
// - Condition flags
// - Execution state controls
// - Exception mask bits
//   - D: Debug exception mask bit
//   - A: SError asynchronous exception mask bit
//   - I: IRQ asynchronous exception mask bit
//   - F: FIQ asynchronous exception mask bit
// - Access control bits
// - Timing control bits
// - Speculation control bits

struct pt_regs {
	unsigned long regs[31];
	unsigned long sp;
	unsigned long pc;
	unsigned long pstate;
};

#endif
