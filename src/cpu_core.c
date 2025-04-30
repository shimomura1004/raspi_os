#include <stddef.h>
#include "cpu_core.h"
#include "sched.h"
#include "utils.h"

struct pcpu_struct pcpus[NUMBER_OF_PCPUS];

void init_pcpu_struct(unsigned long cpuid) {
    // todo: 今のところ使われていないため問題ないが、本来は idle_vm を設定する
    pcpus[cpuid].current_vcpu = NULL;
    pcpus[cpuid].id = cpuid;
    pcpus[cpuid].number_of_off = 0;
    pcpus[cpuid].interrupt_enable = 0;
}

// 現在実行中の pCPU を返す
struct pcpu_struct *current_pcpu() {
	return &pcpus[get_cpuid()];
}

struct pcpu_struct *pcpu_of(unsigned long cpuid) {
	return &pcpus[cpuid];
}
