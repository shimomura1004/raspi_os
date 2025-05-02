#include <stddef.h>
#include "cpu_core.h"
#include "sched.h"
#include "utils.h"

struct pcpu_struct pcpus[NUMBER_OF_PCPUS];

void init_pcpu_struct(unsigned long cpuid) {
    pcpus[cpuid].current_vcpu = NULL;
    pcpus[cpuid].id = cpuid;

    struct vcpu_struct *sched_context = &pcpus[cpuid].scheduler_context;
    sched_context->number_of_off = 0;
    sched_context->interrupt_enable = 1;
    sched_context->vm = NULL;
    init_lock(&sched_context->lock, "scheduler_context");
}

// 現在実行中の pCPU を返す
struct pcpu_struct *current_pcpu() {
	return &pcpus[get_cpuid()];
}

struct pcpu_struct *pcpu_of(unsigned long cpuid) {
	return &pcpus[cpuid];
}
