#ifndef _FIFO_H
#define _FIFO_H

struct fifo;

int is_empty_fifo(struct fifo *);
int is_full_fifo(struct fifo *);
struct fifo *create_fifo(void);
void clear_fifo(struct fifo *);
int enqueue_fifo(struct fifo *, unsigned long);
int dequeue_fifo(struct fifo *, unsigned long *);
int used_of_fifo(struct fifo *);

#endif
