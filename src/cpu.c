#include <stddef.h>
#include "cpu.h"
#include "sched.h"
#include "utils.h"

struct cpu_core_struct cpu_cores[NUMBER_OF_CPU_CORES];

void init_cpu_core(unsigned long id) {
    cpu_cores[id].current_vm = NULL;
    cpu_cores[id].id = id;
    cpu_cores[id].number_of_off = 0;
    cpu_cores[id].interrupt_enable = 0;
}

struct cpu_core_struct *current_cpu_core() {
	return &cpu_cores[get_cpuid()];
}
