#ifndef	_UTILS_H
#define	_UTILS_H

#include <stddef.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

struct cpu_sysregs;

void memzero(void *src, size_t n);
void memcpy(void *dst, const void *src, size_t n);

extern void delay(unsigned long);
extern void put32(unsigned long, unsigned int);
extern unsigned int get32(unsigned long);
extern unsigned long get_el(void);
extern void set_stage2_pgd(unsigned long pgd, unsigned long vmid);
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

int abs(int n);
char *strncpy(char *dst, const char *src, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strdup(const char *s);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memchr(const void *s, int c, size_t n);
char *strchr(const char *s, int c);
char *strcpy(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
int isspace(int c);
int toupper(int c);
int tolower(int c);

#endif  /*_UTILS_H */
