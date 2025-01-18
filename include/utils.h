#ifndef	_UTILS_H
#define	_UTILS_H

#include <stddef.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

struct cpu_sysregs;

void memzero(void *, size_t);
void memcpy(void *, const void *, size_t);

extern void delay(unsigned long);
extern void put32(unsigned long, unsigned int);
extern unsigned int get32(unsigned long);
extern unsigned long get_el(void);
extern void set_stage2_pgd(unsigned long pgd, unsigned long vmid);
extern void set_sp_el2(unsigned long);
// x0 が指すメモリアドレスに保存された値を各システムレジスタに復元する
extern void _set_sysregs(struct cpu_sysregs *);
// 各システムレジスタの値を取り出し、x0 が指すメモリアドレスに保存する
extern void _get_sysregs(struct cpu_sysregs *);

extern void assert_vfiq(void);
extern void assert_virq(void);
extern void assert_vserror(void);
extern void clear_vfiq(void);
extern void clear_virq(void);
extern void clear_vserror(void);

int abs(int);
char *strncpy(char *, const char *, size_t);
size_t strlen(const char *);
size_t strnlen(const char *, size_t);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
char *strdup(const char *);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memchr(const void *, int, size_t);
char *strchr(const char *, int);
char *strcpy(char *, const char *);
char *strncat(char *, const char *, size_t);
char *strcat(char *, const char *);
int isdigit(int);
int isspace(int);
int toupper(int);
int tolower(int);

#endif  /*_UTILS_H */
