#ifndef	_TIMER_H
#define	_TIMER_H

void timer_init ( void );
void handle_timer_irq ( void );
unsigned long get_physical_timer_count(void);

#endif  /*_TIMER_H */
