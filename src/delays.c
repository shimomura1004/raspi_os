/*
 * Copyright (C) 2018 bzt (bztsrc@github)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */


#include "peripherals/systimer.h"
#include "utils.h"

/**
 * Wait N CPU cycles (ARM CPU only)
 */
void wait_cycles(unsigned int n) {
    if (n) {
        while (n--) {
            asm volatile("nop");
        }
    }
}

// cntfrq_el0: Clock frequency. Indicates the system counter clock frequency in Hz.
/**
 * Wait N microsec (ARM CPU only)
 */
void wait_msec(unsigned int n) {
    register unsigned long f, t, r;
    // get the current counter frequency
    asm volatile("mrs %0, cntfrq_el0" : "=r"(f));
    // read the current counter
    asm volatile("mrs %0, cntpct_el0" : "=r"(t));
    // calculate expire value for counter
    t += ((f / 1000) * n) / 1000;
    do {
        asm volatile("mrs %0, cntpct_el0" : "=r"(r));
    } while (r < t);
}

/**
 * Get System Timer's counter
 */
unsigned long get_system_timer() {
    unsigned int h = -1, l;
    // we must read MMIO area as two separate 32 bit reads
    h = get32(TIMER_CHI);
    l = get32(TIMER_CLO);
    // we have to repeat it if high word changed during read
    if (h != get32(TIMER_CHI)) {
        h = get32(TIMER_CHI);
        l = get32(TIMER_CLO);
    }
    // compose long int value
    return ((unsigned long)h << 32) | l;
}

/**
 * Wait N microsec (with BCM System Timer)
 */
void wait_msec_st(unsigned int n) {
    unsigned long t = get_system_timer();
    // we must check if it's non-zero, because qemu does not emulate
    // system timer, and returning constant zero would mean infinite loop
    if (t) {
        while (get_system_timer() < t + n)
            ;
    }
}
