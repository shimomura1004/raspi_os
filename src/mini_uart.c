#include "peripherals/mini_uart.h"
#include "peripherals/gpio.h"
#include "printf.h"
#include "utils.h"
#include "sched.h"
#include "fifo.h"
#include "vm.h"
#include "systimer.h"

static void _uart_send(char c) {
    // 送信バッファが空くまで待つビジーループ
    while (1) {
        if (get32(AUX_MU_LSR_REG) & 0x20) {
            break;
        }
    }
    put32(AUX_MU_IO_REG, c);
}

void uart_send(char c) {
    if (c == '\n' || c == '\r') {
        _uart_send('\r');
        _uart_send('\n');
    } else {
        _uart_send(c);
    }
}

char uart_recv(void) {
    // 受信バッファにデータが届くまで待つビジーループ
    while (1) {
        if (get32(AUX_MU_LSR_REG) & 0x01) {
            break;
        }
    }

	char c = get32(AUX_MU_IO_REG) & 0xFF;
	if (c == '\r') {
		return '\n';
	}
    return c;
}

// このハイパーバイザのエスケープ文字
#define ESCAPE_CHAR '?'

// UART から入力されたデータを送り込む先の VM の番号(0 のときはホスト)
static int uart_forwarded_vm = 0;

int is_uart_forwarded_vm(struct vm_struct *tsk) {
    return tsk->vmid == uart_forwarded_vm;
}

void uart_send_string(char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        _uart_send((char)str[i]);
    }
}

// uart_forwarded_vm が指す VM かホストに文字データを追加する
// todo: キューに値が入っているときは、そのゲストに切り替わったときに仮想割り込みを発生させる
void handle_uart_irq(void) {
    static int is_escaped = 0;

    char received = get32(AUX_MU_IO_REG) & 0xff;
    struct vm_struct *tsk;

    if (is_escaped) {
        is_escaped = 0;

        if (isdigit(received)) {
            // VM を切り替えるのではなく、単に UART 入力の送り先を変えるだけ
            uart_forwarded_vm = received - '0';
            printf("\nswitched to %d\n", uart_forwarded_vm);
            tsk = vms[uart_forwarded_vm];
            if (tsk->state == VM_RUNNING || tsk->state == VM_RUNNABLE) {
                flush_vm_console(tsk);
            }
        }
        else if (received == 'l') {
            show_vm_list();
        }
        else if (received == 't') {
            show_systimer_info();
        }
        else if (received == ESCAPE_CHAR) {
            goto enqueue_char;
        }
    }
    else if (received == ESCAPE_CHAR) {
        is_escaped = 1;
    }
    else {
enqueue_char:
        tsk = vms[uart_forwarded_vm];
        // もし VM が終了してしまっていたら無視する
        if (tsk->state == VM_RUNNING ||  tsk->state == VM_RUNNABLE) {
            enqueue_fifo(tsk->console.in_fifo, received);
        }
    }
}

void uart_init(void) {
    unsigned int selector;

    selector = get32(GPFSEL1);
    selector &= ~(7 << 12); // clean gpio14
    // 2(0b010) は alt5 を選ぶという意味
    selector |= 2 << 12;    // set alt5 for gpio14
    selector &= ~(7 << 15); // clean gpio15
    selector |= 2 << 15;    // set alt5 for gpio15
    put32(GPFSEL1, selector);

    // GPIO のpull-up/pull-down の設定をクリアする
    put32(GPPUD, 0);
    delay(150);
    put32(GPPUDCLK0, (1 << 14) | (1 << 15));
    delay(150);
    put32(GPPUDCLK0, 0);

    // UART としての設定を行う
    put32(AUX_ENABLES, 1);          // Enable mini uart
                                    // (this also enables access to its registers)
    put32(AUX_MU_CNTL_REG, 0);      // Disable auto flow control and disable
                                    // receiver and transmitter (for now)
    put32(AUX_MU_IER_REG, 1);       // Enable receive interrupts
    put32(AUX_MU_LCR_REG, 3);       // Enable 8 bit mode
    put32(AUX_MU_MCR_REG, 0);       // Set RTS line to be always high
    put32(AUX_MU_BAUD_REG, 270);    // Set baud rate to 115200

    put32(AUX_MU_CNTL_REG, 3); // Finally, enable transmitter and receiver
}

// This function is required by printf function
void putc(void *p, char c) { _uart_send(c); }
