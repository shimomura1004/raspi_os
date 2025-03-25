#ifndef _HYPERCALL_H
#define _HYPERCALL_H

void hypercall(unsigned long hvc_nr, unsigned long a0, unsigned long a1, unsigned long a2, unsigned long a3);

#endif
