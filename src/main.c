#include <stddef.h>
#include <stdint.h>

#include "printf.h"
#include "utils.h"
#include "timer.h"
#include "irq.h"
#include "task.h"
#include "sched.h"
#include "mini_uart.h"
#include "mm.h"
#include "sd.h"
#include "debug.h"
#include "loader.h"
#include "spinlock.h"

unsigned long initialized_flag = 0;
struct spinlock test_lock;
struct spinlock log_lock;

// hypervisor としてのスタート地点
// todo: マルチコアでここに入ってくるとクラッシュする
void hypervisor_main()
{
	unsigned long cpuid = get_cpuid();
	unsigned long sp = get_sp();

	// CPU0 が初期化を実施
	if (cpuid == 0) {
		initialized_flag = 1;
		while(1);
	}
	else if (cpuid == 1) {
		// todo: CPU1 が初期化を実施すること自体は問題ないが、vm に切り替えて
		//       kernel exit で eret するときに pc が 0 になってしまう
		//       -> これは想定どおりで、VM は 0 番地から開始する想定になっているはず
		//       -> vm 入るときにアドレス空間が切り替わっていないのが問題
		// sched.c:set_cpu_sysregs の中で restore_sysregs を呼び
		//   それが msr ttbr0_el1, x2 を実行しているが、このとき x2 が 0 になっている
		//   -> まだゲスト os は動いていなくて設定してないから正常
		// -> 2段階アドレス変換が有効になっていない(ゲストの IPA 0 が PA 0 になっている)
		uart_init();
		init_printf(NULL, putc);

		printf("=== raspvisor ===\n");

		// ホスト用のコンソールの初期化
		init_task_console(current);

		init_initial_task();

		irq_vector_init();
		timer_init();

		// 中途半端なところで割込み発生しないようにタイマと UART の有効化が終わるまで割込み禁止
		disable_irq();
		enable_interrupt_controller();
		enable_irq();

		if (sd_init() < 0) {
			PANIC("sd_init() failed");
		}

		// todo: 他の OS のロード

		// img には entrypoint などの情報がないので自分で入れる必要がある
		struct raw_binary_loader_args bl_args1 = {
			.loader_addr = 0x0,
			.entry_point = 0x0,
			.sp = 0x100000,
			.filename = "ECHO.BIN",
		};
		if (create_task(raw_binary_loader, &bl_args1) < 0) {
			printf("error while starting task #1");
			return;
		}

		// struct raw_binary_loader_args bl_args2 = {
		// 	.loader_addr = 0x0,
		// 	.entry_point = 0x0,
		// 	.sp = 0x100000,
		// 	.filename = "MINI-OS.BIN",
		// };
		// if (create_task(raw_binary_loader, &bl_args2) < 0) {
		// 	printf("error while starting task #2");
		// 	return;
		// }

		// struct raw_binary_loader_args bl_args3 = {
		// 	.loader_addr = 0x0,
		// 	.entry_point = 0x0,
		// 	.sp = 0xffff000000100000,
		// 	.filename = "MINI-OS.ELF",
		// };
		// if (create_task(elf_binary_loader, &bl_args3) < 0) {
		// 	printf("error while starting task #3");
		// 	return;
		// }

		// struct raw_binary_loader_args bl_args4 = {
		// 	.loader_addr = 0x0,
		// 	.entry_point = 0x0,
		// 	.sp = 0x100000,
		// 	.filename = "ECHO.ELF",
		// };
		// if (create_task(elf_binary_loader, &bl_args4) < 0) {
		// 	printf("error while starting task #4");
		// 	return;
		// }

		// struct raw_binary_loader_args bl_args5 = {
		// 	.loader_addr = 0x0,
		// 	.entry_point = 0x0,
		// 	.sp = 0x100000,
		// 	.filename = "MINI-OS.BIN",
		// };
		// if (create_task(raw_binary_loader, &bl_args5) < 0) {
		// 	printf("error while starting task #5");
		// 	return;
		// }

init_lock(&test_lock, "test_lock");
init_lock(&log_lock, "log_lock");
printf("start running other threads\n");

		// コア0以外のブロックを解除
		// initialized_flag = 1;
	}
	else {
		// todo: 別スレッドでログ出力するとクラッシュする
		// INFO("CPUID: %d, SP: 0x%lx", cpuid, get_sp());
		INFO("@@@");
		while (1) ;
	}
INFO("DONE");
	while (1){
		// todo: schedule を呼ぶ前に手動で割込みを禁止にしないといけないのは危ない
		disable_irq();
		// このプロセスでは特にやることがないので CPU を明け渡す
		// todo: ゲスト OS のコントロールができるといい
		schedule();
		enable_irq();
	}
}
