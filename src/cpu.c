#include <stddef.h>
#include "cpu.h"
#include "sched.h"
#include "utils.h"

struct cpu_struct cpus[NUMBER_OF_CPUS];

static void init_cpu(unsigned long id) {
    cpus[id].current = NULL;
    cpus[id].id = id;
    cpus[id].number_of_off = 0;
    cpus[id].interrupt_enable = 0;
}

void init_my_cpu() {
    init_cpu(get_cpuid());
}

struct cpu_struct *current_cpu() {
	return &cpus[get_cpuid()];
}
