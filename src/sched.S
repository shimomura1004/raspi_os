#include "sched.h"

// #define THREAD_CPU_CONTEXT			0 		// offset of cpu_context in task_struct
// 実際 cpu_context は構造体の先頭に配置されているのでオフセットは 0
// struct task_struct {
// 	struct cpu_context cpu_context;
// 	long state;	
// 	long counter;
// 	long priority;
// 	long preempt_count;
// 	unsigned long flags;
// 	struct mm_struct mm;
// };

// struct cpu_context {
// 	unsigned long x19;
// 	unsigned long x20;
// 	unsigned long x21;
// 	unsigned long x22;
// 	unsigned long x23;
// 	unsigned long x24;
// 	unsigned long x25;
// 	unsigned long x26;
// 	unsigned long x27;
// 	unsigned long x28;
// 	unsigned long fp;
// 	unsigned long sp;
// 	unsigned long pc;
// };


// #define THREAD_CPU_CONTEXT			0 		// offset of cpu_context in task_struct 
// extern void cpu_switch_to(struct task_struct* prev, struct task_struct* next);
.globl cpu_switch_to
cpu_switch_to:
	// THREAD_CPU_CONTEXT は task_struct 構造体の中の cpu_context のオフセット
	// x0 には current のアドレスが入っている(つまり、切り替え元の VM の構造体)
	mov	x10, #THREAD_CPU_CONTEXT
	// 切り替え元の vm_struct 構造体のアドレス(x0)にオフセットを足したものを x8 に入れる
	// つまり切り替え元の VM の cpu_context のアドレスが x8 に入る
	add	x8, x0, x10
	// 今のスタックポインタを x9 に入れる
	mov	x9, sp
	// 呼び出された側で保存しないといけないレジスタをメモリに控える
	// これ以外のレジスタは cpu_switch_to を呼び出すときに呼び出し元でスタック上に控えられている
	//   #16 はポストインクリメントで
	//   2つのレジスタを x8 の指すアドレスに保存したあと x8 の値を16増やす
	stp	x19, x20, [x8], #16		// store callee-saved registers
	stp	x21, x22, [x8], #16
	stp	x23, x24, [x8], #16
	stp	x25, x26, [x8], #16
	stp	x27, x28, [x8], #16
	stp	x29, x9, [x8], #16      // cpu_context.fp <= x29, cpu_context.sp <= x9(=sp)
	// x30 はリンクレジスタであり、戻り先アドレスが入っている、これを pc に入れる 
	str	x30, [x8]               // cpu_context.pc <= x30
	// 切り替え先の vm_struct 構造体のアドレス(x1)にオフセットを足したものを x8 に入れる
	// つまり切り替え先の VM の cpu_context のアドレスが x8 に入る
	add	x8, x1, x10
	// 同様にレジスタを戻す
	ldp	x19, x20, [x8], #16		// restore callee-saved registers
	ldp	x21, x22, [x8], #16
	ldp	x23, x24, [x8], #16
	ldp	x25, x26, [x8], #16
	ldp	x27, x28, [x8], #16
	ldp	x29, x9, [x8], #16
	ldr	x30, [x8]
	// スタックポインタも戻す
	mov	sp, x9
	// x30 (リンクレジスタ)の保持するアドレスに戻る
	ret


