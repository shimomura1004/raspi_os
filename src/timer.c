#include "utils.h"
#include "sched.h"
#include "peripherals/timer.h"

// RPi3 には 1tick ごとにカウントアップするタイマが搭載されていて
// 合計4個の比較用レジスタがあり、カウンタの値が一致すると対応する割込み線を発火させる

const unsigned int interval = 20000;
unsigned int current_value = 0;

void timer_init ( void )
{
	// 今のカウンタの値を取り出す
	current_value = get32(TIMER_CLO);
	current_value += interval;
	// interval tick 後に発火するように2個目のレジスタに値をセットする
	put32(TIMER_C1, current_value);
}

void handle_timer_irq( void ) 
{
	current_value += interval;
	put32(TIMER_C1, current_value);
	// TIMER_CS: timer control/status register
	// todo: "acknowledge" がわからないが、割込みをクリアする？
	put32(TIMER_CS, TIMER_CS_M1);
	timer_tick();
}
