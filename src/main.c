#include <stddef.h>
#include <stdint.h>

// todo: 不要なものがないか確認
#include "printf.h"
#include "utils.h"
#include "systimer.h"
#include "irq.h"
#include "vm.h"
#include "sched.h"
#include "mini_uart.h"
#include "mm.h"
#include "sd.h"
#include "debug.h"
#include "loader.h"
#include "peripherals/irq.h"
#include "peripherals/mailbox.h"
#include "cpu_core.h"

// boot.S で初期化が終わるまでコアを止めるのに使うフラグ
volatile unsigned long initialized_flag = 0;

// todo: どこか適切な場所に移す
struct spinlock log_lock = {0, "log_lock", -1};

// // todo: 他の種類の OS のロード
// // この情報は、あとから VM にコンテキストスイッチしたときに参照される
// // そのときまで解放されないようにグローバル変数としておく
// // img には entrypoint などの情報がないので自分で入れる必要がある
// static struct loader_args echo_bin_args = {
// 	.loader_addr = 0x0,
// 	.entry_point = 0x0,
// 	.sp = 0x100000,
// 	.filename = "ECHO.BIN",
// };
// struct loader_args mini_os_bin_args = {
// 	.loader_addr = 0x0,
// 	.entry_point = 0x0,
// 	.sp = 0x100000,
// 	.filename = "MINI-OS.BIN",
// };
// struct loader_args mini_os_elf_args = {
// 	.loader_addr = 0x0,
// 	.entry_point = 0x0,
// 	.sp = 0xffff000000100000,
// 	.filename = "MINI-OS.ELF",
// };
// struct loader_args echo_elf_args = {
// 	.loader_addr = 0x0,
// 	.entry_point = 0x0,
// 	.sp = 0x100000,
// 	.filename = "ECHO.ELF",
// };
// struct loader_args test_bin_args = {
// 	.loader_addr = 0x0,
// 	.entry_point = 0x0,
// 	.sp = 0x100000,
// 	.filename = "TEST2.BIN",
// };
//
// struct loader_args raspios_bin_args = {
// 	.loader_addr = 0x0,
// 	.entry_point = 0x0,
// 	.sp = 0x100000,
// 	.filename = "RASPIOS.BIN",
// };
// struct loader_args raspios_elf_args = {
// 	.loader_addr = 0x0,
// 	.entry_point = 0x0,
// 	.sp = 0xffff000000100000,
// 	.filename = "RASPIOS.ELF",
// };

struct loader_args vmm_elf_args = {
	.loader_addr = 0x0,
	.entry_point = 0x0,
	.sp = 0xffff000000100000,
	.filename = "VMM.ELF",
};

// 各 pCPU で必要な初期化処理
static void initialize_pcpu(unsigned long cpuid) {
	// CPU コア構造体の初期化
	init_cpu_core_struct(cpuid);

	// // CPU コアごとの idle vm を作成
	// create_idle_vm(cpuid);

	// VBAR_EL2 レジスタに割込みベクタのアドレスを設定する
	// 各 CPU コアで呼び出す必要がある
	irq_vector_init();
}

// 全コア共通で一度だけ実施する初期化処理
static void initialize_hypervisor() {
	// initiate_idle_vms();
	mm_init();
	uart_init();
	init_printf(NULL, putc);

	// システムタイマは全コアで共有されるのでここで初期化
	// todo: generic timer にする
	systimer_init();

	// Core 1~3 の Core 0 からの MAILBOX の割込みを有効化
	put32(MBOX_CORE1_CONTROL, MBOX_CONTROL_IRQ_0_BIT);
	put32(MBOX_CORE2_CONTROL, MBOX_CONTROL_IRQ_0_BIT);
	put32(MBOX_CORE3_CONTROL, MBOX_CONTROL_IRQ_0_BIT);

	// 中途半端なところで割込み発生しないようにタイマと UART の有効化が終わるまで割込み禁止
	disable_irq();
	enable_interrupt_controller();
	enable_irq();

	// SD カードの初期化
	if (sd_init() < 0) {
		PANIC("sd_init() failed");
	}
}

// todo: このへんは dom0 相当のゲストで実装すべき
static void prepare_guest_vms() {
	if (create_vm_with_loader(elf_binary_loader, &vmm_elf_args) < 0) {
		printf("error while starting VMM\n");
	}

	// if (create_vm_with_loader(raw_binary_loader, &echo_bin_args) < 0) {
	// 	printf("error while starting VM #1");
	// }

	// if (create_vm_with_loader(raw_binary_loader, &mini_os_bin_args) < 0) {
	// 	printf("error while starting VM #2");
	// }

	// if (create_vm_with_loader(elf_binary_loader, &mini_os_elf_args) < 0) {
	// 	printf("error while starting VM #3");
	// }

	// if (create_vm_with_loader(elf_binary_loader, &echo_elf_args) < 0) {
	// 	printf("error while starting VM #4");
	// }

	// if (create_vm_with_loader(raw_binary_loader, &test_bin_args) < 0) {
	// 	printf("error while starting VM #5");
	// }

	// if (create_vm_with_loader(elf_binary_loader, &echo_elf_args) < 0) {
	// 	printf("error while starting VM #6");
	// }

	// if (create_vm_with_loader(raw_binary_loader, &raspios_bin_args) < 0) {
	// 	printf("error while starting VM #6");
	// }

	// if (create_vm_with_loader(elf_binary_loader, &raspios_elf_args) < 0) {
	// 	printf("error while starting VM #6");
	// }
}

// hypervisor としてのスタート地点
void hypervisor_main(unsigned long cpuid)
{
	// 実行中の CPU コアを初期化
	initialize_pcpu(cpuid);

	// CPU 0 がハイパーバイザの初期化を実施
	if (cpuid == 0) {
		// ハイパーバイザの初期化とゲストのロードを実施
		initialize_hypervisor();
		INFO("Raspvisor initialized");

		create_idle_vm(cpuid);
		INFO("Idle VM and idle vCPUs are created");

		prepare_guest_vms();
		INFO("guest VMs are prepared");

		initialized_flag = 1;
	}

	INFO("CPU%d runs IDLE vCPU", cpuid);

	scheduler(cpuid);
}
