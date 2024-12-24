#include <inttypes.h>
#include "board.h"
#include "debug.h"
#include "bcm2837.h"
#include "mm.h"
#include "fifo.h"
#include "peripherals/mini_uart.h"
#include "peripherals/timer.h"
#include "peripherals/irq.h"

// BCM2837 SoC を表現するデータ構造と関数群
// ハイパーバイザでは BCM2837 をエミュレートする

// BCM2837 の内部レジスタ
struct bcm2837_state {
    // 7 Interrupt controller -> 7.5 Registers
    struct intctrl_regs {
        uint8_t irq_basic_pending;
        uint8_t irq_pending_1;
        uint8_t irq_pending_2;
        uint8_t fiq_control;
        uint8_t enable_irqs_1;
        uint8_t enable_irqs_2;
        uint8_t enable_basic_irqs;
        uint8_t disable_irqs_1;
        uint8_t disable_irqs_2;
        uint8_t disable_basic_irqs;
    } intctrl;

    // aux_* は UART1, SPI1, SPI2 に関連するレジスタ
    struct aux_peripherals_regs {
        struct fifo *mu_tx_fifo;
        struct fifo *mu_rx_fifo;
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

    // BCM2837-ARM-Peripherals.-.Revised.-.V2-1.pdf
    // CS Register
    //   [31:4] Reserved
    //   [3]    Timer 3 match
    //          0: No Timer 3 match since last cleared
    //          1: Timer 3 match detected
    //   [2]    Timer 2 match
    //   ...
    struct systimer_regs {
        uint32_t cs;        // System Timer Control/Status
        uint32_t clo;       // System Timer Counter Lower 32 bits
        uint32_t chi;       // System Timer Counter Higher 32 bits
        uint32_t c0;        // System Timer Compare 0
        uint32_t c1;        // System Timer Compare 1
        uint32_t c2;        // System Timer Compare 2
        uint32_t c3;        // System Timer Compare 3
    } systimer;
};

// BCM2837-ARM-Peripherals.-.Revised.-.V2-1.pdf
// AUX_MU_IO_REG
//   [31:8] Reserved
//   [7:0]  Data
//     DLAB=1, RW: Access the LS bits of the 16-bit baudrate register
//     DLAB=0, W: Data written is put in the transmit FIFO
//     DLAB=0, R: Data read is taken from the receive FIFO
// AUX_MU_IIR_REG: Interrupt Identity Register?
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
// AUX_MU_LCR_REG: Line Control Register?
//   [31:8] Reserved
//   [7]    DLAB asscess
//          If set the first to Mini UART register give access
//          the Bauderate register. During operation this bit must be cleared.
//   [6]    Break
//          If set high the UART1_TX line is pulled low continuously.
//          If held for at least 12 bits times that will indicate a break condition.
//   [5:2]  Reserved
//   [1:0]  Data size
//            00: the UART works in 7-bit mode
//            11: the UART works in 8-bit mode
// AUX_MU_LSR_REG: Line Status Register?
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
// AUX_MU_MSR_REG: Modem Status Register?
//   [31:8] Reserved
//   [7:6]  Reserved
//   [5]    CTS status
//          This bit is the inverse of the UART1_CTS input Thus:
//            If set the UART1_CTS pin is low
//            If clear the UART1_CTS pin is high
//   [4]?
//   [3:0]  reserved
const struct bcm2837_state initial_state = {
    .intctrl = {
        .irq_basic_pending   = 0x0,
        .irq_pending_1       = 0x0,
        .irq_pending_2       = 0x0,
        .fiq_control         = 0x0,
        .enable_irqs_1       = 0x0,
        .enable_irqs_2       = 0x0,
        .enable_basic_irqs   = 0x0,
        .disable_irqs_1      = 0x0,
        .disable_irqs_2      = 0x0,
        .disable_basic_irqs  = 0x0,
    },
    .aux = {
        // fifo の初期化は bcm2837_initialize で行う
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

    state->aux.mu_tx_fifo = create_fifo();
    state->aux.mu_rx_fifo = create_fifo();

    tsk->board_data = state;

    // デバイスの初期化(MMIO ページの準備)
    unsigned long begin = DEVICE_BASE;
    unsigned long end = PHYS_MEMORY_SIZE - SECTION_SIZE;
    for (; begin < end; begin += PAGE_SIZE) {
        set_task_page_notaccessable(tsk, begin);
    }
}

static unsigned long handle_intctrl_read(unsigned long addr, int accsz) {
    struct bcm2837_state *state = (struct bcm2837_state *)current->board_data;

    switch (addr) {
    case IRQ_BASIC_PENDING:
        return state->intctrl.irq_basic_pending;
    case IRQ_PENDING_1:
        return state->intctrl.irq_pending_1;
    case IRQ_PENDING_2:
        return state->intctrl.irq_pending_2;
    case FIQ_CONTROL:
        return state->intctrl.fiq_control;
    case ENABLE_IRQS_1:
        return state->intctrl.enable_irqs_1;
    case ENABLE_IRQS_2:
        return state->intctrl.enable_irqs_2;
    case ENABLE_BASIC_IRQS:
        return state->intctrl.enable_basic_irqs;
    case DISABLE_IRQS_1:
        return state->intctrl.disable_irqs_1;
    case DISABLE_IRQS_2:
        return state->intctrl.disable_irqs_2;
    case DISABLE_BASIC_IRQS:
        return state->intctrl.disable_basic_irqs;
    }

    return 0;
}

static void handle_intctrl_write(unsigned long addr, unsigned long val, int accsz) {
    struct bcm2837_state *state = (struct bcm2837_state *)current->board_data;

    // 書き込めるレジスタは限定されている
    switch (addr) {
    case FIQ_CONTROL:
        state->intctrl.fiq_control = val;
        break;
    case ENABLE_IRQS_1:
        state->intctrl.enable_irqs_1 = val;
        break;
    case ENABLE_IRQS_2:
        state->intctrl.enable_irqs_2 = val;
        break;
    case ENABLE_BASIC_IRQS:
        state->intctrl.enable_basic_irqs = val;
        break;
    case DISABLE_IRQS_1:
        state->intctrl.disable_irqs_1 = val;
        break;
    case DISABLE_IRQS_2:
        state->intctrl.disable_irqs_2 = val;
        break;
    case DISABLE_BASIC_IRQS:
        state->intctrl.disable_basic_irqs = val;
        break;
    }
}

static unsigned long handle_aux_read(unsigned long addr, int accsz) {
    struct bcm2837_state *state = (struct bcm2837_state *)current->board_data;

    if ((state->aux.aux_enables & 0x1) == 0 && ADDR_IN_AUX(addr)) {
        return 0;
    }

    switch (addr) {
    case AUX_IRQ:
        return state->aux.aux_irq;
    case AUX_ENABLES:
        return state->aux.aux_enables;
    case AUX_MU_IO_REG:
        // LCR の 8 ビット目を確認
        if (AUX_MU_LCR_REG & 0x80) {
            // todo: baudrate の下位 8 ビットを返すべき
        return state->aux.aux_mu_io;
        }
        else {
            unsigned long data;
            dequeue_fifo(state->aux.mu_rx_fifo, &data);
            return data;
        }
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

static void handle_aux_write(unsigned long addr, unsigned long val, int accsz) {
    struct bcm2837_state *state = (struct bcm2837_state *)current->board_data;

    if ((state->aux.aux_enables & 0x1) == 0 && ADDR_IN_AUX(addr)) {
        return;
    }

    switch (addr) {
    case AUX_ENABLES:
        state->aux.aux_enables = val;
        break;
    case AUX_MU_IO_REG:
        if (AUX_MU_LCR_REG & 0x80) {
            // todo: baudrate の下位 8 ビットを設定するべき
            state->aux.aux_mu_io = val;
        }
        else {
            enqueue_fifo(state->aux.mu_tx_fifo, val && 0xff);
        }
        state->aux.aux_mu_io = val;
        break;
    case AUX_MU_IER_REG:
        state->aux.aux_mu_ier = val;
        break;
    case AUX_MU_IIR_REG:
        state->aux.aux_mu_iir = val;
        break;
    case AUX_MU_LCR_REG:
        state->aux.aux_mu_lcr = val;
        break;
    case AUX_MU_MCR_REG:
        state->aux.aux_mu_mcr = val;
        break;
    case AUX_MU_LSR_REG:
        state->aux.aux_mu_lsr = val;
        break;
    case AUX_MU_MSR_REG:
        state->aux.aux_mu_msr = val;
        break;
    case AUX_MU_SCRATCH:
        state->aux.aux_mu_scratch = val;
        break;
    case AUX_MU_CNTL_REG:
        state->aux.aux_mu_cntl = val;
        break;
    case AUX_MU_STAT_REG:
        state->aux.aux_mu_stat = val;
        break;
    case AUX_MU_BAUD_REG:
        state->aux.aux_mu_baud = val;
        break;
    }
}

static unsigned long handle_systimer_read(unsigned long addr, int accsz) {
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

// todo: 戻り値は void では
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
