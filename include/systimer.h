#ifndef	_TIMER_H
#define	_TIMER_H

void systimer_init(void);
void handle_systimer1_irq(void);
void handle_systimer3_irq(void);
unsigned long get_physical_systimer_count(void);
void show_systimer_info(void);

#endif  /*_TIMER_H */
