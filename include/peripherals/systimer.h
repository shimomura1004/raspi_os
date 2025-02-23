#ifndef	_P_TIMER_H
#define	_P_TIMER_H

#include "peripherals/base.h"

// System Timer Registers
// CS: System Timer Control/Status
//       Write a one to the relevant bit to clear the match
//       detect status bit and the corresponding interrupt request line. 
//   [31:4] unused
//   [3] M3: System Timer Match 3
//     0b0: No Timer 3 match since last cleared.
//     0b1: Timer 3 match detected
//   [2] M2: System Timer Match 2
//   [1] M1: System Timer Match 1
//   [0] M0: System Timer Match 0
// CLO: System Timer Counter Lower bits
//   [31:0] Lower 32-bits of the free running counter value
// CHI: System Timer Counter Higher bits
//   [31:0] Higher 32-bits of the free running counter value
// C0, C1, C2, C3: System Timer Compare
//   [31:0] Compare value for match channel n

#define TIMER_CS        (PBASE+0x00003000)
#define TIMER_CLO       (PBASE+0x00003004)
#define TIMER_CHI       (PBASE+0x00003008)
#define TIMER_C0        (PBASE+0x0000300C)
#define TIMER_C1        (PBASE+0x00003010)
#define TIMER_C2        (PBASE+0x00003014)
#define TIMER_C3        (PBASE+0x00003018)

#define TIMER_CS_M0	(1 << 0)
#define TIMER_CS_M1	(1 << 1)
#define TIMER_CS_M2	(1 << 2)
#define TIMER_CS_M3	(1 << 3)

#endif  /*_P_TIMER_H */
