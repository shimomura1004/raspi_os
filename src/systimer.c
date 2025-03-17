#include "utils.h"
#include "sched.h"
#include "printf.h"
#include "peripherals/systimer.h"
#include "peripherals/mailbox.h"

// todo: これは system timer である
//       コアごとにある generic timer ではない
//       VM の切り替えには generic timer を使うべき

// RPi3 には 1tick ごとにカウントアップするタイマが搭載されていて
// 合計4個の比較用レジスタがあり、カウンタの値が一致すると対応する割込み線を発火させる

const unsigned int interval = 20000;

void systimer_init () {
	// 今のカウンタ値から interval tick 後に発火するように2個目のレジスタに値をセットする
	put32(TIMER_C1, get32(TIMER_CLO) + interval);
}

// VM スイッチ用
void handle_systimer1_irq() {
	// 定期的に呼び出されるよう、次の比較値をセットする
	put32(TIMER_C1, get32(TIMER_CLO) + interval);
	// 割込みをクリア
	put32(TIMER_CS, TIMER_CS_M1);

	// CPU0 の VM 切り替え
	timer_tick();

	// CPU0 以外のコアに mbox 割込みを送ってタスクを切り替えさせる
	put32(MBOX_CORE1_SET_0, 0x1);
	put32(MBOX_CORE2_SET_0, 0x1);
	put32(MBOX_CORE3_SET_0, 0x1);
}

// VM の割込み用
void handle_systimer3_irq() {
	// 割込みをクリア
	put32(TIMER_CS, TIMER_CS_M3);
}

// システムタイマのレジスタ CLO/CHI を読み、合わせて64ビット値として返す
unsigned long get_physical_systimer_count() {
	unsigned long clo = get32(TIMER_CLO);
	unsigned long chi = get32(TIMER_CHI);
	return clo | (chi << 32);
}

void show_systimer_info() {
	printf("HI: 0x%x\nLO: 0x%x\nCS: 0x%x\nC1: 0x%x\nC3: 0x%x\n",
	get32(TIMER_CHI), get32(TIMER_CLO), get32(TIMER_CS), get32(TIMER_C1), get32(TIMER_C3));
}
