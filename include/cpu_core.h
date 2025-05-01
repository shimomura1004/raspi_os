#ifndef _CPU_CORE_H
#define _CPU_CORE_H

#include "vm.h"

#define NUMBER_OF_PCPUS 4

struct pcpu_struct {
    unsigned long id;                   // この CPU コアの ID
    struct vcpu_struct *current_vcpu;   // この CPU コアが実行している vCPU

    // この CPU コアが実行しているハイパーバイザ(EL2)のコンテキスト
    struct vcpu_struct scheduler_context;
};

void init_pcpu_struct(unsigned long cpuid);
struct pcpu_struct *current_pcpu();
struct pcpu_struct *pcpu_of(unsigned long cpuid);

#endif
