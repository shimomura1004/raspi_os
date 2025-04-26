.section ".text.boot"

#include "../../../include/hypercall_type.h"

.globl _start
_start:
  mov x8, 777
  hvc #HYPERCALL_TYPE_WARN_LU

  ldr x8, hello_str
  hvc #HYPERCALL_TYPE_INFO_LX
  hvc #HYPERCALL_TYPE_INFO_STR

  ldr x8, echo_bin_args_p
  hvc #HYPERCALL_TYPE_CREATE_VM_FROM_ELF
1:
  b 1b
