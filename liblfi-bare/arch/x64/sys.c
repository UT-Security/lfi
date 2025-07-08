#include <assert.h>
#include <stdint.h>

#include "engine.h"
#include "lfi.h"
#include "sys.h"
#include "types.h"

#include "arch_sys.h"

enum {
    TUX_ARCH_SET_FS = 0x1002,
};

static int
sys_arch_prctl(struct TuxThread* p, int code, uintptr_t addr)
{
    switch (code) {
    case TUX_ARCH_SET_FS:
        lfi_ctx_tpset(p->p_ctx, addr);
        return 0;
    default:
        return -TUX_EINVAL;
    }
}

void
arch_syshandle(struct LFIContext* ctx)
{
    struct TuxThread* p = (struct TuxThread*) lfi_ctx_data(ctx);
    struct TuxProc* proc = p->proc;
    struct TuxRegs* regs = lfi_ctx_regs(ctx);

    uint64_t orig_rax = regs->rax;

    switch (regs->rax) {
    case TUX_SYS_arch_prctl:
        regs->rax = sys_arch_prctl(p, regs->rdi, regs->rsi);
        break;
    default:
        // Generic syscalls.
        regs->rax = syshandle(p, regs->rax, regs->rdi, regs->rsi, regs->rdx,
                regs->r10, regs->r8, regs->r9);
    }
}
