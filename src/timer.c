#include "utils.h"
#include "sched.h"
#include "printf.h"
#include "peripherals/timer.h"

// RPi3 には 1tick ごとにカウントアップするタイマが搭載されていて
// 合計4個の比較用レジスタがあり、カウンタの値が一致すると対応する割込み線を発火させる

const unsigned int interval = 20000;
unsigned int current_value = 0;

void timer_init () {
	// 今のカウンタの値を取り出す
	current_value = get32(TIMER_CLO);
	current_value += interval;
	// interval tick 後に発火するように2個目のレジスタに値をセットする
	put32(TIMER_C1, current_value);
}

// タスクスイッチ用
void handle_timer1_irq() {
	// 定期的に呼び出されるよう、次の比較値をセットする
	current_value += interval;
	put32(TIMER_C1, current_value);
	// 割込みをクリア
	put32(TIMER_CS, TIMER_CS_M1);
	timer_tick();
}

// VM の割込み用
void handle_timer3_irq() {
	// 割込みをクリア
	put32(TIMER_CS, TIMER_CS_M3);
}

// システムタイマのレジスタ CLO/CHI を読み、合わせて64ビット値として返す
unsigned long get_physical_timer_count() {
	unsigned long clo = get32(TIMER_CLO);
	unsigned long chi = get32(TIMER_CHI);
	return clo | (chi << 32);
}

void show_systimer_info() {
	printf("HI: 0x%x\nLO: 0x%x\nCS: 0x%x\nC1: 0x%x\nC3: 0x%x\n",
	get32(TIMER_CHI), get32(TIMER_CLO), get32(TIMER_CS), get32(TIMER_C1), get32(TIMER_C3));
}
