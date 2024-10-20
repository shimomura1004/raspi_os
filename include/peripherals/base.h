#ifndef	_P_BASE_H
#define	_P_BASE_H

#include "mm.h"

// ドライバのアドレスは 0xffff00003f000000 から始まる
#define DEVICE_BASE 		0x3F000000	
#define PBASE 			(VA_START + DEVICE_BASE)

#endif  /*_P_BASE_H */
