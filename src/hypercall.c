#include "mm.h"
#include "loader.h"
#include "hypercall.h"
#include "hypercall_type.h"
#include "debug.h"

void hypercall(unsigned long hvc_nr, unsigned long a0, unsigned long a1, unsigned long a2, unsigned long a3) {
    struct pt_regs *regs = vm_pt_regs(current_cpu_core()->current_vm);

    switch (hvc_nr) {
	case HYPERCALL_TYPE_WARN_LU: {
		WARN("HVC #%lu", hvc_nr);
		break;
    }
	case HYPERCALL_TYPE_INFO_LX: {
		INFO("HVC #%d: 0x%lx(%ld)", hvc_nr, a0, a0);
		break;
    }
	case HYPERCALL_TYPE_INFO_LX_LX: {
		INFO("HVC #%d: 0x%lx(%ld), 0x%lx(%ld)", hvc_nr, a0, a0, a1, a1);
		break;
    }
	case HYPERCALL_TYPE_INFO_LX_LX_LX: {
		INFO("HVC #%d: 0x%lx(%ld), 0x%lx(%ld), 0x%lx(%ld)", hvc_nr, a0, a0, a1, a1, a2, a2);
		break;
    }
	case HYPERCALL_TYPE_INFO_LX_LX_LX_LX: {
		INFO("HVC #%d: 0x%lx(%ld), 0x%lx(%ld), 0x%lx(%ld), 0x%lx(%ld)", hvc_nr, a0, a0, a1, a1, a2, a2, a3, a3);
		break;
    }

	case HYPERCALL_TYPE_INFO_STR: {
		INFO("HVC #%d: %s", hvc_nr, (const char *)get_pa_2nd(a0));
		break;
	}

	case HYPERCALL_TYPE_CREATE_VM_FROM_ELF: {
		// VM が動くときに使うデータなのでスタック上にとってはいけない
		// todo: 同時に複数の OS が起動すると競合するため、専用のヒープ領域に確保するべき
		static char filename[128];

		// ゲストのメモリに依存しないようハイパーバイザ側にコピー
		struct raw_binary_loader_args args = *(struct raw_binary_loader_args *)get_pa_2nd(a0);

		// 文字列ポインタはネストしてアドレス変換が必要、変換しつつ static 変数上にコピーする
		memcpy(&filename, (const char *)get_pa_2nd((unsigned long)args.filename), 128);
		args.filename = filename;

		INFO("Prepare VM(%s) by hypercall", args.filename, args.filename);
        int vmid = create_vm_with_loader(elf_binary_loader, &args);
        regs->regs[8] = vmid;
		break;
    }

    default:
		WARN("uncaught hvc64 exception: %ld", hvc_nr);
		break;
	}
}
