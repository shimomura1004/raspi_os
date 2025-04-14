#ifndef _CPU_CORE_H
#define _CPU_CORE_H

#include "vm.h"

#define NUMBER_OF_CPU_CORES 4

struct cpu_core_struct {
    unsigned long id;               // この CPU コアの ID
    struct vm_struct *current_vm;   // この CPU コアが実行している VM

    // この CPU コアが実行しているハイパーバイザ(EL2)のコンテキスト
    struct vm_struct scheduler_context;

    // 排他制御時に割込みを禁止するとき、何回割込み禁止が要求されたかを保持する
    // 全員が割込み禁止を解除したら、本当に割込みを許可する
    int number_of_off;
    int interrupt_enable;
};

void init_cpu_core_struct(unsigned long cpuid);
struct cpu_core_struct *current_cpu_core();
struct cpu_core_struct *cpu_core(unsigned long cpuid);

#endif
