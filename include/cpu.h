#ifndef _CPU_H

#define NUMBER_OF_CPUS 4

struct cpu_core_struct {
    unsigned long id;                   // この CPU の ID
    struct task_struct *current_vm;     // この CPU が実行している VM

    // 排他制御時に割込みを禁止するとき、何回割込み禁止が要求されたかを保持する
    // 全員が割込み禁止を解除したら、本当に割込みを許可する
    int number_of_off;
    // todo: 使ってない、削除する？
    int interrupt_enable;
};

void init_cpu_core(unsigned long cpuid);
struct cpu_core_struct *current_cpu_core();

#endif
