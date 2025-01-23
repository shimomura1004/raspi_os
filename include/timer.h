#ifndef	_TIMER_H
#define	_TIMER_H

void timer_init(void);
void handle_timer1_irq(void);
void handle_timer3_irq(void);
unsigned long get_physical_timer_count(void);
void show_systimer_info(void);

#endif  /*_TIMER_H */
