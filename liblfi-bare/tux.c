#include <stdlib.h>

#include "lfi_tux.h"
#include "lfi.h"
#include "proc.h"
#include "engine.h"
#include "types.h"

#include "arch_sys.h"

EXPORT void
lfi_tux_syscall(struct LFIContext* ctx)
{
    arch_syshandle(ctx);
}

EXPORT struct Tux*
lfi_tux_new(struct LFIPlatform* plat, struct TuxOptions opts)
{
    struct Tux* tux = malloc(sizeof(struct Tux));
    if (!tux)
        return NULL;
    *tux = (struct Tux) {
        .plat = plat,
        .opts = opts,
    };

    lfi_sys_handler(plat, &lfi_tux_syscall);
    return tux;
}

EXPORT uint64_t
lfi_tux_proc_run(struct TuxThread* p)
{
    return lfi_ctx_run(p->p_ctx, p->proc->p_as);
}

EXPORT void
lfi_tux_libinit(struct Tux* tux, bool val)
{
    tux->opts.libinit = val;
}

extern void lfi_ctx_internal(void)
    asm ("lfi_ctx_internal");
