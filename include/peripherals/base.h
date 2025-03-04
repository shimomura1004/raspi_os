#ifndef	_P_BASE_H
#define	_P_BASE_H

#include "mm.h"

// これは Arm コア側からみたアドレス(物理アドレス)
//   videocore からみたアドレスもある(バスアドレス)
// https://www.reddit.com/r/osdev/comments/uc98tz/raspberry_pi_3_base_peripheral_address/#:~:text=Physical%20addresses%20range%20from%200x3F000000,address%20range%20starting%20at%200x7E000000.
// RPi3B では、ドライバの物理アドレスは 0x000000003f000000 から始まる
#define DEVICE_BASE 		0x3F000000	
#define PBASE 			(VA_START + DEVICE_BASE)

#endif  /*_P_BASE_H */
