#include "peripherals/base.h"
#include "peripherals/mini_uart.h"
#include "peripherals/timer.h"

.org 0x0

.globl el1_test_1
el1_test_1:
    ldr x0, =(TIMER_CS) // Control Status Register のアドレスを x0 にロード
    mov w8, 777
    str w8, [x0]        // Control Status Register に 777 を書き込む
    str w8, [x0]
    str w8, [x0]
    hvc #0
1:
    b 1b

.globl el1_test_2
el1_test_2:
    hvc #0
2:
    b 2b
