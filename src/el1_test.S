#include "peripherals/base.h"
#include "peripherals/mini_uart.h"
#include "peripherals/systimer.h"

.org 0x0

.globl el1_test_1
el1_test_1:
    ldr x0, =(TIMER_CS) // Control Status Register のアドレスを x0 にロード
    // ハイパーバイザコールの番号 #0 は使っていない
    // todo: この x0 はハンドラによって esr に書き換えられてしまうので
    //       システムコールに渡せていないのでは？ 
    hvc #0
1:
    b 1b

.globl el1_test_2
el1_test_2:
    hvc #0
2:
    b 2b
