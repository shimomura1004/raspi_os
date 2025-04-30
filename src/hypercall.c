#include "mm.h"
#include "loader.h"
#include "hypercall.h"
#include "hypercall_type.h"
#include "debug.h"

void hypercall(unsigned long hvc_nr, unsigned long a0, unsigned long a1, unsigned long a2, unsigned long a3) {
    struct pt_regs *regs = vcpu_pt_regs(current_cpu_core()->current_vcpu);

    switch (hvc_nr) {
	case HYPERCALL_TYPE_WARN_LU: {
		WARN("HVC #%lu(%lu)", hvc_nr, a0);
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
		// 最初にこの VM に CPU 時間が割当たったタイミングで arg が使用される
		// よってゲストのメモリに依存しないようハイパーバイザ側にコピーしておく
		struct loader_args args = *(struct loader_args *)get_pa_2nd(a0);

		INFO("Prepare VM(%s) by hypercall", args.filename);
        int vmid = create_vm_with_loader(elf_binary_loader, &args);
        regs->regs[8] = vmid;
		break;
    }

    default:
		WARN("uncaught hvc64 exception: %ld", hvc_nr);
		break;
	}
}
