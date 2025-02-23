#include <stddef.h>
#include "cpu.h"
#include "sched.h"
#include "utils.h"

struct cpu_core_struct cpus[NUMBER_OF_CPUS];

void init_cpu_core(unsigned long id) {
    cpus[id].current_vm = NULL;
    cpus[id].id = id;
    cpus[id].number_of_off = 0;
    cpus[id].interrupt_enable = 0;
}

struct cpu_core_struct *current_cpu_core() {
	return &cpus[get_cpuid()];
}
