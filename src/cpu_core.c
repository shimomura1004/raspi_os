#include <stddef.h>
#include "cpu_core.h"
#include "sched.h"
#include "utils.h"

struct cpu_core_struct cpu_cores[NUMBER_OF_CPU_CORES];

void init_cpu_core_struct(unsigned long cpuid) {
    // todo: 今のところ使われていないため問題ないが、本来は idle_vm を設定する
    cpu_cores[cpuid].current_vm = NULL;
    cpu_cores[cpuid].id = cpuid;
    cpu_cores[cpuid].number_of_off = 0;
    cpu_cores[cpuid].interrupt_enable = 0;
}

struct cpu_core_struct *current_cpu_core() {
	return &cpu_cores[get_cpuid()];
}

struct cpu_core_struct *cpu_core(unsigned long cpuid) {
	return &cpu_cores[cpuid];
}