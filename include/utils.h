#ifndef	_UTILS_H
#define	_UTILS_H

struct cpu_sysregs;

void memzero(unsigned long src, unsigned long n);
void memcpy(unsigned long dst, unsigned long src, unsigned long n);
void memset(unsigned long dst, int val, unsigned long n);

extern void delay ( unsigned long);
extern void put32 ( unsigned long, unsigned int );
extern unsigned int get32 ( unsigned long );
extern unsigned long get_el ( void );
extern void set_stage2_pgd(unsigned long pgd, unsigned long vmid);
// x0 が指すメモリアドレスに保存された値を各システムレジスタに復元する
extern void _set_sysregs(struct cpu_sysregs *);
// 各システムレジスタの値を取り出し、x0 が指すメモリアドレスに保存する
extern void _get_sysregs(struct cpu_sysregs *);

#endif  /*_UTILS_H */
