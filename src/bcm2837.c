#include <inttypes.h>
#include "board.h"
#include "debug.h"
#include "bcm2837.h"
#include "mm.h"
#include "peripherals/mini_uart.h"
#include "peripherals/timer.h"

// BCM2837 SoC を表現するデータ構造と関数群
// ハイパーバイザでは BCM2837 をエミュレートする

// BCM2837 の内部レジスタ
struct bcm2837_state {
    struct aux_peripherals_regs {
        uint8_t  aux_irq;
        uint8_t  aux_enables;
        uint8_t  aux_mu_io;
        uint8_t  aux_mu_ier;
        uint8_t  aux_mu_iir;
        uint8_t  aux_mu_lcr;
        uint8_t  aux_mu_mcr;
        uint8_t  aux_mu_lsr;
        uint8_t  aux_mu_msr;
        uint8_t  aux_mu_scratch;
        uint8_t  aux_mu_cntl;
        uint32_t aux_mu_stat;
        uint16_t aux_mu_baud;
    } aux;

    struct systimer_regs {
        uint32_t cs;
        uint32_t clo;
        uint32_t chi;
        uint32_t c0;
        uint32_t c1;
        uint32_t c2;
        uint32_t c3;
    } systimer;
};

// BCM2837-ARM-Peripherals.-.Revised.-.V2-1.pdf
// IIR: Interrupt Identity Register?
//   [31:8] Reserved
//   [7:6]  FIFO enables
//   [5:4]  Always read as zero
//   [3]    Always read as zero as the mini UART has no timeout function
//   [2:1]  READ: Interrupt ID bits
//                00: No interrupts
//                01: Transmit holding register empty
//                10: Receiver holds valid byte
//                11: Not possible
//          WRITE: FIFO clear bits
//                Writing with bit 1 set will clear the receive FIFO
//                Writing with bit 2 set will clear the transmit FIFO
//   [0]    Interrupt pending
//          This bit is clear whenever an interrupt is pending
// LSR: Line Status Register?
//   [31:8] Reserved
//   [7]    Reserved
//   [6]    Transmitter idle
//          This bit is set if the transmit FIFO is empty and the transmitter is idle.
//   [5]    Transmitter empty
//          This bit is set if the transmit FIFO can accept at least one byte.
//   [4:2]  Reserved
//   [1]    Receiver Overrun
//          This bit is set if there was a receiver overrun.
//          - one or more characters arrived whilst the receive FIFO was full.
//          - The newly arrived character was discarded.
//          This bit is cleared each time this register is read.
//   [0]    Data ready
//          This bit is set if the receive FIFO holds at least 1 symbol
// MSR: Modem Status Register?
//   [31:8] Reserved
//   [7:6]  Reserved
//   [5]    CTS status
//          This bit is the inverse of the UART1_CTS input Thus:
//            If set the UART1_CTS pin is low
//            If clear the UART1_CTS pin is high
//   [4]?
//   [3:0]  reserved
const struct bcm2837_state initial_state = {
    .aux = {
        .aux_irq        = 0x0,
        .aux_enables    = 0x0,
        .aux_mu_io      = 0x0,
        .aux_mu_ier     = 0x0,
        .aux_mu_iir     = 0xc1,     // 0xc1 はリセット時の初期値
        .aux_mu_lcr     = 0x0,
        .aux_mu_mcr     = 0x0,
        .aux_mu_lsr     = 0x40,     // 0x40 はリセット時の初期値
        .aux_mu_msr     = 0x20,     // 0x20 はリセット時の初期値
        .aux_mu_scratch = 0x0,
        .aux_mu_cntl    = 0x3,      // 0x3 はリセット時の初期値
        .aux_mu_stat    = 0x30c,    // 0x30c はリセット時の初期値
        .aux_mu_baud    = 0x0,
    },
    .systimer = {
        .cs  = 0x0,
        .clo = 0x0,
        .chi = 0x0,
        .c0  = 0x0,
        .c1  = 0x0,
        .c2  = 0x0,
        .c3  = 0x0,
    },
};

void bcm2837_initialize(struct task_struct *tsk) {
    struct bcm2837_state *state = (struct bcm2837_state *)allocate_page();
    *state = initial_state;
    tsk->board_data = state;

    // デバイスの初期化(MMIO ページの準備)
    unsigned long begin = DEVICE_BASE;
    unsigned long end = PHYS_MEMORY_SIZE - SECTION_SIZE;
    for (; begin < end; begin += PAGE_SIZE) {
        set_task_page_notaccessable(tsk, begin);
    }
}

unsigned long handle_mini_uart_read(unsigned long addr, int accsz) {
    struct bcm2837_state *state = (struct bcm2837_state *)current->board_data;

    switch (addr) {
    case AUX_IRQ:
        return state->aux.aux_irq;
    case AUX_ENABLES:
        return state->aux.aux_enables;
    case AUX_MU_IO_REG:
        return state->aux.aux_mu_io;
    case AUX_MU_IER_REG:
        return state->aux.aux_mu_ier;
    case AUX_MU_IIR_REG:
        return state->aux.aux_mu_iir;
    case AUX_MU_LCR_REG:
        return state->aux.aux_mu_lcr;
    case AUX_MU_MCR_REG:
        return state->aux.aux_mu_mcr;
    case AUX_MU_LSR_REG:
        return state->aux.aux_mu_lsr;
    case AUX_MU_MSR_REG:
        return state->aux.aux_mu_msr;
    case AUX_MU_SCRATCH:
        return state->aux.aux_mu_scratch;
    case AUX_MU_CNTL_REG:
        return state->aux.aux_mu_cntl;
    case AUX_MU_STAT_REG:
        return state->aux.aux_mu_stat;
    case AUX_MU_BAUD_REG:
        return state->aux.aux_mu_baud;
    }

    return 0;
}

unsigned long handle_mini_uart_write(unsigned long addr, unsigned long val, int accsz) {
    struct bcm2837_state *state = (struct bcm2837_state *)current->board_data;

    switch (addr) {
    case AUX_ENABLES:
        return state->aux.aux_enables = val;
    case AUX_MU_IO_REG:
        return state->aux.aux_mu_io = val;
    case AUX_MU_IER_REG:
        return state->aux.aux_mu_ier = val;
    case AUX_MU_IIR_REG:
        return state->aux.aux_mu_iir = val;
    case AUX_MU_LCR_REG:
        return state->aux.aux_mu_lcr = val;
    case AUX_MU_MCR_REG:
        return state->aux.aux_mu_mcr = val;
    case AUX_MU_LSR_REG:
        return state->aux.aux_mu_lsr = val;
    case AUX_MU_MSR_REG:
        return state->aux.aux_mu_msr = val;
    case AUX_MU_SCRATCH:
        return state->aux.aux_mu_scratch = val;
    case AUX_MU_CNTL_REG:
        return state->aux.aux_mu_cntl = val;
    case AUX_MU_STAT_REG:
        return state->aux.aux_mu_stat = val;
    case AUX_MU_BAUD_REG:
        return state->aux.aux_mu_baud = val;
    }

    return 0;
}

unsigned long handle_systimer_read(unsigned long addr, int accsz) {
    struct bcm2837_state *state = (struct bcm2837_state *)current->board_data;

    switch (addr) {
    case TIMER_CS:
        return state->systimer.cs;
    case TIMER_CLO:
        return state->systimer.clo;
    case TIMER_CHI:
        return state->systimer.chi;
    case TIMER_C0:
        return state->systimer.c0;
    case TIMER_C1:
        return state->systimer.c1;
    case TIMER_C2:
        return state->systimer.c2;
    case TIMER_C3:
        return state->systimer.c3;
    }

    return 0;
}

unsigned long handle_systimer_write(unsigned long addr, unsigned long val, int accsz) {
    struct bcm2837_state *state = (struct bcm2837_state *)current->board_data;

    switch (addr) {
    case TIMER_CS:
        return state->systimer.cs = val;
    case TIMER_CLO:
        return state->systimer.clo = val;
    case TIMER_CHI:
        return state->systimer.chi = val;
    case TIMER_C0:
        return state->systimer.c0 = val;
    case TIMER_C1:
        return state->systimer.c1 = val;
    case TIMER_C2:
        return state->systimer.c2 = val;
    case TIMER_C3:
        return state->systimer.c3 = val;
    }

    return 0;
}

// mmio 領域へのアクセスがあった場合、アドレスに応じてアクセスするデバイスを切りかえる
unsigned long bcm2837_mmio_read(unsigned long addr, int accsz) {
    if (AUX_IRQ <= addr && addr <= AUX_MU_BAUD_REG) {
        return handle_mini_uart_read(addr, accsz);
    }
    else if (TIMER_CS <= addr && addr < TIMER_C3) {
        return handle_systimer_read(addr, accsz);
    }

    return 0;
}

void bcm2837_mmio_write(unsigned long addr, unsigned long val, int accsz) {
    if (AUX_IRQ <= addr && addr <= AUX_MU_BAUD_REG) {
        handle_mini_uart_write(addr, val, accsz);
    }
    else if (TIMER_CS <= addr && addr < TIMER_C3) {
        handle_systimer_write(addr, val, accsz);
    }
}

const struct board_ops bcm2837_board_ops = {
    .initialize = bcm2837_initialize,
    .mmio_read = bcm2837_mmio_read,
    .mmio_write = bcm2837_mmio_write,
};
