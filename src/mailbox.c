#include "peripherals/mailbox.h"
#include "sched.h"
#include "debug.h"

void handle_mailbox_irq(unsigned long cpuid) {
    // INFO("MAILBOX!");
    timer_tick();
}
