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

	case HYPERCALL_TYPE_CREATE_VM: {
		// todo: ポインタで渡されてもそのままアクセスすることはできない
        int vmid = create_vm_with_loader((loader_func_t)a0, (void*)a1);
        regs->regs[8] = vmid;
		break;
    }

    default:
		WARN("uncaught hvc64 exception: %ld", hvc_nr);
		break;
	}
}
