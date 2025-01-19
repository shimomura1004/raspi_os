#include <inttypes.h>
#include "board.h"
#include "debug.h"
#include "bcm2837.h"
#include "mm.h"
#include "fifo.h"
#include "timer.h"
#include "utils.h"
#include "peripherals/mini_uart.h"
#include "peripherals/timer.h"
#include "peripherals/irq.h"

// BCM2837 SoC を表現するデータ構造と関数群
// ハイパーバイザでは BCM2837 をエミュレートする

// BCM2837 の内部レジスタ
// todo: 実機にあるのにここで定義されないレジスタは、別の状態から都度計算しているから？確認する
struct bcm2837_state {
    // 7 Interrupt controller -> 7.5 Registers
    // FIQ register
    //   [31:8] unused
    //   [7]    FIQ enable
    //   [6:0]  Select FIQ Source
    //     0-63   : GPU interrupts
    //     64     : ARM Timer interrupt
    //     65     : ARM Mailbox interrupt
    //     66     : ARM Doorbell 0 interrupt
    //     67     : ARM Doorbell 1 interrupt
    //     68     : GPU0 Halted interrupt
    //     69     : GPU1 Halted interrupt
    //     70     : Illegal access type-1 interrupt
    //     71     : Illegal access type-0 interrupt
    //     72-127 : Do Not Use
    struct intctrl_regs {
        // Arm peripherals interrupt table(IRQ 0-64, Timer, Mailbox...)
        uint8_t irq_enabled[72];
        uint8_t fiq_control;
        uint8_t irqs_1_enabled;
        uint8_t irqs_2_enabled;
        uint8_t basic_irqs_enabled;
    } intctrl;

    // aux_* は UART1, SPI1, SPI2 に関連するレジスタ
    struct aux_peripherals_regs {
        int      mu_rx_overrun;
        uint8_t  aux_enables;
        uint8_t  aux_mu_io;         // todo: 不要では？
        uint8_t  aux_mu_ier;
        uint8_t  aux_mu_lcr;
        uint8_t  aux_mu_mcr;
        uint8_t  aux_mu_msr;
        uint8_t  aux_mu_scratch;
        uint8_t  aux_mu_cntl;
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
        uint64_t last_physical_count;   // VM の切り替え前の最後のカウンタの値
        uint64_t offset;                // 実時間と VM が使った時間との差分
        uint32_t cs;        // System Timer Control/Status
        uint32_t c0;        // System Timer Compare 0
        uint32_t c1;        // System Timer Compare 1
        uint32_t c2;        // System Timer Compare 2
        uint32_t c3;        // System Timer Compare 3
        uint64_t c0_long;
        uint64_t c1_long;
        uint64_t c2_long;
        uint64_t c3_long;
    } systimer;
};

// BCM2837-ARM-Peripherals.-.Revised.-.V2-1.pdf
// AUX_MU_IO_REG
//   [31:8] Reserved
//   [7:0]  Data
//     DLAB=1, RW: Access the LS bits of the 16-bit baudrate register
//     DLAB=0, W: Data written is put in the transmit FIFO
//     DLAB=0, R: Data read is taken from the receive FIFO
// AUX_MU_IER_REG: Interrupt Enable Register
//   [31:8] Reserved
//   DLAB=1
//   [7:0]  Enable Transmit FIFO interrupt
//   DLAB=0
//   [7:4]  Ignored
//   [3:2]  must be set to 1 to receive interrupts
//   [1]    Enable transmit interrupts
//   [0]    Enable receive interrupts
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
//   [5]    CTS status (CTS: clear to send)
//          This bit is the inverse of the UART1_CTS input Thus:
//            If set the UART1_CTS pin is low
//            If clear the UART1_CTS pin is high
//          CTS と RTS はクロスして通信相手とつなぎ、CTS が low のときに送信可能という意味
//   [4]?
//   [3:0]  reserved
const struct bcm2837_state initial_state = {
    .intctrl = {
        .fiq_control         = 0x0,
        .irqs_1_enabled       = 0x0,
        .irqs_2_enabled       = 0x0,
        .basic_irqs_enabled   = 0x0,
    },
    .aux = {
        // fifo の初期化は bcm2837_initialize で行う
        .mu_rx_overrun  = 0,
        .aux_enables    = 0x0,
        .aux_mu_io      = 0x0,
        .aux_mu_ier     = 0x0,
        .aux_mu_lcr     = 0x0,
        .aux_mu_mcr     = 0x0,
        // todo: MSR.CTS=1 にしたいなら 0x20(0b0010_0000) では？
        .aux_mu_msr     = 0x10,     // AUX_M_MSR_REG.CTS=1 なので UART1_CTS=0、つまり送信可能
        .aux_mu_scratch = 0x0,
        .aux_mu_cntl    = 0x3,      // 0x3 はリセット時の初期値
        .aux_mu_baud    = 0x0,
    },
    .systimer = {
        .cs      = 0x0,
        .c0      = 0x0,
        .c1      = 0x0,
        .c2      = 0x0,
        .c3      = 0x0,
    },
};

#define ADDR_IN_INTCTRL(a)  (IRQ_BASIC_PENDING <= (a) && (a) <= DISABLE_BASIC_IRQS)
#define ADDR_IN_AUX(a)      (AUX_IRQ <= (a) && (a) <= AUX_MU_BAUD_REG)
#define ADDR_IN_SYSTIMER(a) (TIMER_CS <= (a) && (a) < TIMER_C3)

static void bcm2837_initialize(struct task_struct *tsk) {
    struct bcm2837_state *state = (struct bcm2837_state *)allocate_page();

    *state = initial_state;

    state->systimer.last_physical_count = get_physical_timer_count();

    tsk->board_data = state;

    // stage2 のデバイスのメモリマッピング(MMIO ページの準備)
    unsigned long begin = DEVICE_BASE;
    unsigned long end = PHYS_MEMORY_SIZE - SECTION_SIZE;
    for (; begin < end; begin += PAGE_SIZE) {
        set_task_page_notaccessable(tsk, begin);
    }
}

// Registers and their offsets for interrupts
// 0x200: IRQ basic pending  
// 0x204: IRQ pending 1  
// 0x208: IRQ pending 2  
// 0x20C: FIQ control  
// 0x210: Enable IRQs 1  
// 0x214: Enable IRQs 2  
// 0x218: Enable Basic IRQs  
// 0x21C: Disable IRQs 1  
// 0x220: Disable IRQs 2  
// 0x224: Disable Basic IRQs

// Basic pending register
//   [31:21] Unused
//   [20:10] GPU IRQ 62, 57, 56, 55, 54, 53, 19, 18, 10, 9, 7
//   [9] One or more bits set in pending register 2
//   [8] One or more bits set in pending register 1
//   [7] Illegal access type 0 IRQ pending
//   [6] Illegal access type 1 IRQ pending
//   [5] GPU 1 halted IRQ pending
//   [4] GPU 0 halted IRQ pending
//   [3] ARM Doorbell 1 IRQ pending
//   [2] ARM Doorbell 0 IRQ pending
//   [1] Arm Mailbox IRQ pending
//   [0] Arm Timer IRQ pending
// GPU pending 1 register (IRQ pending register?)
//   [31:0] IRQ pending source 31:0 (See IRQ table above)
// GPU pending 2 register (IRQ pending register?)
//   [31:0] IRQ pending source 63:32 (See IRQ table above)

static unsigned long handle_aux_read(struct task_struct *tsk, unsigned long addr);
#define BIT(v, n) ((v) & (1 << (n)))
static unsigned long handle_intctrl_read(struct task_struct *tsk, unsigned long addr) {
    struct bcm2837_state *state = (struct bcm2837_state *)tsk->board_data;

    switch (addr) {
    case IRQ_BASIC_PENDING: {
        // todo: 8,9 ビット目以外のフィールドの実装が必要
        int pending1 = handle_intctrl_read(tsk, IRQ_PENDING_1) != 0;
        int pending2 = handle_intctrl_read(tsk, IRQ_PENDING_2) != 0;
        return (pending1 << 8) | (pending2 << 9);
    }
    case IRQ_PENDING_1: {
        unsigned long systimer_match1 =
            BIT(state->intctrl.irqs_1_enabled, 1) && (state->systimer.cs & 0x02);
        unsigned long systimer_match3 =
            BIT(state->intctrl.irqs_1_enabled, 3) && (state->systimer.cs & 0x08);
        // todo: uart の irq 番号は 57 だが、なぜ32を引く？
        //       irqs_register の 2 にわけてあって、後半は32ビット目からのビット数になっているから？
        //       だとしたら BIT(state->intctrl.irqs_2_enabled, (57 - 32)) では？
        unsigned long int uart_int =
            BIT(state->intctrl.irqs_1_enabled, (57 - 32)) &&
            (handle_aux_read(tsk, AUX_IRQ) &  0x01);    // AUXIRQ レジスタの0ビット目が UART
        // todo: BASIC_PENDING や IRQ_PENDING など、割込み関係のレジスタは32ビットしかない
        //       勝手に64ビット値として返してはダメでは？
        return (systimer_match1 << 1) | (systimer_match3 << 3) | (uart_int << 57);
    }
    case IRQ_PENDING_2:
        // todo: 仮実装？ IRQ_PENDING_1 の上位32ビットをこっちで返すべきでは？
        return 0;
    case FIQ_CONTROL:
        return state->intctrl.fiq_control;
    case ENABLE_IRQS_1:
        return state->intctrl.irqs_1_enabled;
    case ENABLE_IRQS_2:
        return state->intctrl.irqs_2_enabled;
    case ENABLE_BASIC_IRQS:
        return state->intctrl.basic_irqs_enabled;
    case DISABLE_IRQS_1:
        return ~state->intctrl.irqs_1_enabled;
    case DISABLE_IRQS_2:
        return ~state->intctrl.irqs_2_enabled;
    case DISABLE_BASIC_IRQS:
        return ~state->intctrl.basic_irqs_enabled;
    }

    return 0;
}

static void handle_intctrl_write(struct task_struct *tsk, unsigned long addr, unsigned long val) {
    struct bcm2837_state *state = (struct bcm2837_state *)tsk->board_data;

    // 書き込みができないレジスタもあるので handle_intctrl_read と一対一で対応しない
    switch (addr) {
    case FIQ_CONTROL:
        state->intctrl.fiq_control = val;
        break;
    case ENABLE_IRQS_1:
        state->intctrl.irqs_1_enabled |= val;
        break;
    case ENABLE_IRQS_2:
        state->intctrl.irqs_2_enabled |= val;
        break;
    case ENABLE_BASIC_IRQS:
        state->intctrl.basic_irqs_enabled |= val;
        break;
    case DISABLE_IRQS_1:
        state->intctrl.irqs_1_enabled &= ~val;
        break;
    case DISABLE_IRQS_2:
        state->intctrl.irqs_2_enabled &= ~val;
        break;
    case DISABLE_BASIC_IRQS:
        state->intctrl.basic_irqs_enabled &= ~val;
        break;
    }
}

#define LCR_DLAB 0x80

static unsigned long handle_aux_read(struct task_struct *tsk, unsigned long addr) {
    struct bcm2837_state *state = (struct bcm2837_state *)tsk->board_data;

    // todo: アドレスが AUX の範囲内で、無効なら return となっている
    //       アドレス範囲外 or 無効なら return が正しいのでは？
    // AUX が無効だったら 0 を返す
    // if ((state->aux.aux_enables & 0x1) == 0 && ADDR_IN_AUX(addr)) {
    //     return 0;
    // }
    if ((state->aux.aux_enables & 0x1) == 0 || !ADDR_IN_AUX(addr)) {
        return 0;
    }

    switch (addr) {
    case AUX_IRQ: {
        // 0 ビット目の UART だけ設定。1,2ビット目の SPI1,2 は未実装
        int mu_pending = (state->aux.aux_enables & 0x1) &&
                          ~(handle_aux_read(tsk, AUX_MU_IIR_REG) & 0x1);
        return mu_pending;
    }
    case AUX_ENABLES:
        return state->aux.aux_enables;
    case AUX_MU_IO_REG:
        if (state->aux.aux_mu_lcr & LCR_DLAB) {
            // todo: なぜ DLAB をクリアする？
            state->aux.aux_mu_lcr &= ~LCR_DLAB;
            // DLAB=1 のときは baudrate の下位 8 ビットを返す
            return state->aux.aux_mu_baud & 0xff;
        }
        else {
            unsigned long data;
            dequeue_fifo(tsk->console.in_fifo, &data);
            return data;
        }
    case AUX_MU_IER_REG:
        if (state->aux.aux_mu_lcr & LCR_DLAB) {
            // DLAB=1 のときは baudrate の上位 8 ビットを返す
            return state->aux.aux_mu_baud >> 8;
        }
        else {
            return state->aux.aux_mu_ier;
        }
    case AUX_MU_IIR_REG: {
        int tx_int = (state->aux.aux_mu_ier & 0x2) && is_empty_fifo(tsk->console.out_fifo);
        int rx_int = (state->aux.aux_mu_ier & 0x1) && !is_empty_fifo(tsk->console.in_fifo);
        int int_id = (tx_int << 0) | (rx_int << 1);
        if (int_id == 0x3) {
            // 仕様上 tx/rx の両方の割込みありで返すことはないので tx だけ割込みありとする
            int_id = 0x1;
        }
        // 0x3 << 6 なので IIR[7:6] FIFO enables は常に有効 
        return (!int_id) | (int_id << 1) | (0x3 << 6);
    }
    case AUX_MU_LCR_REG:
        return state->aux.aux_mu_lcr;
    case AUX_MU_MCR_REG:
        return state->aux.aux_mu_mcr;
    case AUX_MU_LSR_REG: {
        int dready = !is_empty_fifo(tsk->console.in_fifo);
        int rx_overrun = state->aux.mu_rx_overrun;
        int tx_empty = !is_full_fifo(tsk->console.out_fifo);
        int tx_idle = is_empty_fifo(tsk->console.out_fifo);
        // overrun は LSR レジスタを読み込むとクリアされる仕様
        state->aux.mu_rx_overrun = 0;
        // レジスタの値を生成して返す
        return (dready << 0) | (rx_overrun << 1) | (tx_empty << 5) | (tx_idle << 6);
    }
    case AUX_MU_MSR_REG:
        return state->aux.aux_mu_msr;
    case AUX_MU_SCRATCH:
        return state->aux.aux_mu_scratch;
    case AUX_MU_CNTL_REG:
        return state->aux.aux_mu_cntl;
    case AUX_MU_STAT_REG: {
        #define MIN(a, b) ((a) < (b) ? (a) : (b))
        int sym_avail = !is_empty_fifo(tsk->console.in_fifo);
        int space_avail = !is_full_fifo(tsk->console.out_fifo);
        int rx_idle = is_empty_fifo(tsk->console.in_fifo);
        int tx_idle = is_empty_fifo(tsk->console.out_fifo);
        int rx_overrun = state->aux.mu_rx_overrun;
        int tx_full = !space_avail;
        int tx_empty = is_empty_fifo(tsk->console.out_fifo);
        int tx_done = rx_idle & tx_empty;
        int rx_fill_level = MIN(used_of_fifo(tsk->console.in_fifo), 8);
        int tx_fill_level = MIN(used_of_fifo(tsk->console.out_fifo), 8);
        return (sym_avail << 0) | (space_avail << 1) | (rx_idle << 2) | (tx_idle << 3) |
               (rx_overrun << 4) | (tx_full << 5) | (tx_empty << 8) | (tx_done << 9) |
               (rx_fill_level << 16) | (tx_fill_level << 24);
    }
    case AUX_MU_BAUD_REG:
        return state->aux.aux_mu_baud;
    }

    return 0;
}

static void handle_aux_write(struct task_struct *tsk, unsigned long addr, unsigned long val) {
    struct bcm2837_state *state = (struct bcm2837_state *)tsk->board_data;

    // // todo: aux が disable になると、ここにひっかかって enable にすることができないのでは？
    // if ((state->aux.aux_enables & 0x1) == 0 && ADDR_IN_AUX(addr)) {
    //     return;
    // }

    // ↓ DISABLE のときは AUX_ENABLES のみ操作可能
    if (!ADDR_IN_AUX(addr)) {
        return;
    }

    if ((state->aux.aux_enables & 0x1) == 0) {
        if (addr == AUX_ENABLES) {
            state->aux.aux_enables = val;
        }

        return;
    }
    // ↑

    // todo: 一部のレジスタの READ しかできないビットにも値が書き込まれてしまう
    switch (addr) {
    case AUX_ENABLES:
        state->aux.aux_enables = val;
        break;
    case AUX_MU_IO_REG:
        if (state->aux.aux_mu_lcr & LCR_DLAB) {
            // todo: なぜクリア？
            state->aux.aux_mu_lcr &= ~LCR_DLAB;
            // DLAB=1 のときは baudrate の下位 8 ビットを設定
            state->aux.aux_mu_baud = (state->aux.aux_mu_baud & 0xff00) | (val & 0xff);
        }
        else {
            INFO("uart: %c", val & 0xff);
            enqueue_fifo(tsk->console.out_fifo, val & 0xff);
        }
        break;
    case AUX_MU_IER_REG:
        if (state->aux.aux_mu_lcr & LCR_DLAB) {
            // DLAB=1 のときは baudrate の上位 8 ビットを設定
            state->aux.aux_mu_baud = (state->aux.aux_mu_baud & 0x00ff) | ((val & 0xff) << 8);
        }
        else {
            state->aux.aux_mu_ier = val;
        }
        break;
    case AUX_MU_IIR_REG:
        if (val & 0x2) {
            clear_fifo(tsk->console.in_fifo);
        }
        if (val & 0x4) {
            clear_fifo(tsk->console.out_fifo);
        }
        break;
    case AUX_MU_LCR_REG:
        state->aux.aux_mu_lcr = val;
        break;
    case AUX_MU_MCR_REG:
        state->aux.aux_mu_mcr = val;
        break;
    case AUX_MU_SCRATCH:
        state->aux.aux_mu_scratch = val;
        break;
    case AUX_MU_CNTL_REG:
        state->aux.aux_mu_cntl = val;
        break;
    case AUX_MU_BAUD_REG:
        state->aux.aux_mu_baud = val;
        break;
    }
}

// virtual count は、実際に VM が動いている間に進んだ時間(カウント数)を表す
#define TO_VIRTUAL_COUNT(s, p) (p - (s)->systimer.offset)
// physical count は、VM が動いていない間の時間も含めた時間(カウント数)を表す
#define TO_PHYSICAL_COUNT(s, v) (v + (s)->systimer.offset)

// VM からタイマカウントを読み取る(VM が実際に実行された時間を返す)
static unsigned long handle_systimer_read(struct task_struct *tsk, unsigned long addr) {
    struct bcm2837_state *state = (struct bcm2837_state *)tsk->board_data;

    switch (addr) {
    case TIMER_CS:
        return state->systimer.cs;
    case TIMER_CLO:
        return TO_VIRTUAL_COUNT(state, get_physical_timer_count()) & 0xffffffff;
    case TIMER_CHI:
        return TO_VIRTUAL_COUNT(state, get_physical_timer_count()) >> 32;
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

void update_timer_cmpval(unsigned long new_cmpval) {
    // タイマカウンタは64ビット、比較は下位32ビットで行われる
    //   Each channel has an output compare register, which is compared against
    //   the 32 least significant bits of the free running counter values.
    unsigned long current_cmpval = get32(TIMER_C1) | ((unsigned long)get32(TIMER_CHI) << 32);

    // 比較値を今の設定値より小さくしたり、現在のカウンタ値より小さくすることはできない
    // todo: current_cmpval が大きくなったら変更できなくなるのでは？
    if (current_cmpval < new_cmpval &&  get_physical_timer_count() < new_cmpval) {
        put32(TIMER_C1, new_cmpval & 0xffffffff);
    }
}

static void handle_systimer_write(struct task_struct *tsk, unsigned long addr, unsigned long val) {
    struct bcm2837_state *state = (struct bcm2837_state *)tsk->board_data;

// todo: おそらく下位32ビットがマッチする次の64ビットカウンタ値の計算をしたいのだと思うが
//       val は unsigned long で64ビット
//       handle_systimer_read の戻り値も unsigned long だが、実際にはレジスタ値なので32ビット
//       なので、一度カウンタが32ビットの範囲よりも大きくなると、三項演算子の条件部が常に真になる
#define TO_LONG_COMPARE_VALUE(val) \
    val > handle_systimer_read(tsk, TIMER_CLO) ? \
        (handle_systimer_read(tsk, TIMER_CHI) << 32) | val : \
        ((handle_systimer_read(tsk, TIMER_CHI) + 1) << 32) | val

    switch (addr) {
    case TIMER_CS:
        // todo: これは正しい？指定したところだけクリアするような振る舞い
        state->systimer.cs &= ~val;
        break;
    case TIMER_C0:
        state->systimer.c0 = val;
        state->systimer.c0_long = TO_LONG_COMPARE_VALUE(val);
        break;
    case TIMER_C1:
        state->systimer.c1 = val;
        state->systimer.c1_long = TO_LONG_COMPARE_VALUE(val);
        break;
    case TIMER_C2:
        state->systimer.c2 = val;
        state->systimer.c2_long = TO_LONG_COMPARE_VALUE(val);
        break;
    case TIMER_C3:
        state->systimer.c3 = val;
        state->systimer.c3_long = TO_LONG_COMPARE_VALUE(val);
        break;
    }
}

// mmio 領域へのアクセスがあった場合、アドレスに応じてアクセスするデバイスを切りかえる
static unsigned long bcm2837_mmio_read(struct task_struct *tsk, unsigned long addr) {
    if (ADDR_IN_INTCTRL(addr)) {
        return handle_intctrl_read(tsk, addr);
    }
    else if (ADDR_IN_AUX(addr)) {
        return handle_aux_read(tsk, addr);
    }
    else if (ADDR_IN_SYSTIMER(addr)) {
        return handle_systimer_read(tsk, addr);
    }

    return 0;
}

static void bcm2837_mmio_write(struct task_struct *tsk, unsigned long addr, unsigned long val) {
    if (ADDR_IN_INTCTRL(addr)) {
        handle_intctrl_write(tsk, addr, val);
    }
    else if (ADDR_IN_AUX(addr)) {
        handle_aux_write(tsk, addr, val);
    }
    else if (ADDR_IN_SYSTIMER(addr)) {
        handle_systimer_write(tsk, addr, val);
    }
}

// ハイパーバイザでの処理を終えて VM に処理を戻すときに呼ばれる
void bcm2837_entering_vm(struct task_struct *tsk) {
    struct bcm2837_state *state = (struct bcm2837_state *)tsk->board_data;

    // update systimer's offset
    unsigned long current_physical_count = get_physical_timer_count();
    // この VM が動いていない間にすぎた時間を計算し、offset に積算する
    state->systimer.offset += current_physical_count - state->systimer.last_physical_count;

    // update cs register
    unsigned long current_virt_count = TO_VIRTUAL_COUNT(state, current_physical_count);
    int matched = ((current_virt_count >= state->systimer.c0_long) << 0) |
                  ((current_virt_count >= state->systimer.c1_long) << 1) |
                  ((current_virt_count >= state->systimer.c2_long) << 2) |
                  ((current_virt_count >= state->systimer.c3_long) << 3);

    // ~state->systimer.cs: 前回まだ発火していなかったタイマのビットが立っている
    // matched: 今発火したタイマのビットが立っている
    // (~state->systimer.cs) & matched: 今回始めて発火したタイマのビットが立っている
    // todo: 結局 or を取っているだけでは？
    int fired = (~state->systimer.cs) & matched;
    state->systimer.cs |= fired;

    // update (physical) timer compare value for upcoming timer match
    // freerunning カウンタは動き続けるので、比較する場合は physical な値にしないといけない
    update_timer_cmpval(TO_PHYSICAL_COUNT(state, state->systimer.c0_long));
    update_timer_cmpval(TO_PHYSICAL_COUNT(state, state->systimer.c1_long));
    update_timer_cmpval(TO_PHYSICAL_COUNT(state, state->systimer.c2_long));
    update_timer_cmpval(TO_PHYSICAL_COUNT(state, state->systimer.c3_long));
}

// VM での処理を抜けてハイパーバイザに処理に入るときに呼ばれる
void bcm2837_leaving_vm(struct task_struct *tsk) {
    struct bcm2837_state *state = (struct bcm2837_state *)tsk->board_data;
    // VM が実行されていた最後のカウンタ値を保存しておく
    state->systimer.last_physical_count = get_physical_timer_count();
}

static int bcm2837_is_irq_asserted(struct task_struct *tsk) {
    return handle_intctrl_read(tsk, IRQ_BASIC_PENDING) != 0;
}

static int bcm2837_is_fiq_asserted(struct task_struct *tsk) {
    struct bcm2837_state *state = (struct bcm2837_state *)tsk->board_data;

    // FIQ が有効でない場合は常に 0 を返す
    if ((state->intctrl.fiq_control & 0x80) == 0) {
        return 0;
    }

    int source = state->intctrl.fiq_control & 0x7f;
    if (0 <= source && source <= 31) {
        int pending = handle_intctrl_read(tsk, IRQ_PENDING_1);
        return (pending & (1 << source)) != 0;
    }
    else if (32 <= source && source <=63) {
        int pending = handle_intctrl_read(tsk, IRQ_PENDING_2);
        return (pending & (1 << (source - 32))) != 0;
    }
    else if (64 <= source && source <= 71) {
        int pending = handle_intctrl_read(tsk, IRQ_BASIC_PENDING);
        return (pending & (1 << (source - 64))) != 0;
    }

    return 0;
}

const struct board_ops bcm2837_board_ops = {
    .initialize = bcm2837_initialize,
    .mmio_read = bcm2837_mmio_read,
    .mmio_write = bcm2837_mmio_write,
    .entering_vm = bcm2837_entering_vm,
    .leaving_vm = bcm2837_leaving_vm,
    .is_irq_asserted = bcm2837_is_irq_asserted,
    .is_fiq_asserted = bcm2837_is_fiq_asserted,
};
