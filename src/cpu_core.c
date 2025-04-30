#include <stddef.h>
#include "cpu_core.h"
#include "sched.h"
#include "utils.h"

struct pcpu_struct cpu_cores[NUMBER_OF_PCPUS];

void init_cpu_core_struct(unsigned long cpuid) {
    // todo: 今のところ使われていないため問題ないが、本来は idle_vm を設定する
    cpu_cores[cpuid].current_vcpu = NULL;
    cpu_cores[cpuid].id = cpuid;
    cpu_cores[cpuid].number_of_off = 0;
    cpu_cores[cpuid].interrupt_enable = 0;
}

// 現在実行中の pCPU を返す
struct pcpu_struct *current_cpu_core() {
	return &cpu_cores[get_cpuid()];
}

struct pcpu_struct *cpu_core(unsigned long cpuid) {
	return &cpu_cores[cpuid];
}
