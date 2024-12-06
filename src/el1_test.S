.org 0x0
//.section ".text.user"

// todo: .globl el1_test_entry?
.globl entry_el1_entry
el1_test_entry:
    mov x8, #123
    hvc #0
    mov x8, #456
    hvc #0
1:
    b 1b