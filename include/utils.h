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
extern unsigned long translate_el1(unsigned long);
extern unsigned long get_ttbr0_el1();
extern unsigned long get_ttbr1_el1();
extern unsigned long get_ttbr0_el2();
extern unsigned long get_vttbr_el2();
extern unsigned long get_cpuid();
extern unsigned long get_sp();

// Stage2 変換テーブルをセットしてアドレス空間(VTTBR_EL2)を切り替え、つまり IPA -> PA の変換テーブルを切り替える
//   テーブル自体の準備は VM がロードされた初期化時やメモリアボート時に行う
// VM ごとにアドレスの上位8ビットが異なるようになっている
extern void set_stage2_pgd(unsigned long pgd, unsigned long vmid);
// x0 が指すメモリアドレスに保存された値を各システムレジスタに復元する
extern void restore_sysregs(struct cpu_sysregs *);
// 各システムレジスタの値を取り出し、x0 が指すメモリアドレスに保存する
extern void save_sysregs(struct cpu_sysregs *);
// すべての各システムレジスタの値を取り出し、x0 が指すメモリアドレスに保存する
extern void get_all_sysregs(struct cpu_sysregs *);

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
