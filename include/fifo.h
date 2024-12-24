#ifndef _FIFO_H
#define _FIFO_H

struct fifo;

int is_empty_fifo(struct fifo *);
int is_full_fifo(struct fifo *);
struct fifo *create_fifo(void);
void enqueue_fifo(struct fifo *, unsigned long);
int dequeue_fifo(struct fifo *, unsigned long *);

#endif
