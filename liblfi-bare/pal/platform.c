#include <assert.h>
#include <unistd.h>
#include <stdlib.h>

#include "lfi.h"
#include "pal/platform.h"

#define asm __asm__

EXPORT struct LFIPlatform*
lfi_new_plat(struct LFIPlatOptions opts)
{
    struct LFIPlatform* plat = malloc(sizeof(struct LFIPlatform));
    if (!plat)
        return NULL;

    *plat = (struct LFIPlatform) {
        .opts = opts,
    };
    return plat;
}

EXPORT void
lfi_sys_handler(struct LFIPlatform* plat, SysHandlerFn fn)
{
    plat->syshandler = fn;
}

void lfi_syscall_handler(struct LFIContext* ctx)
    asm ("lfi_syscall_handler");

EXPORT void
lfi_syscall_handler(struct LFIContext* ctx)
{
    assert(ctx->as->plat->syshandler && "platform does not have a system call handler");
    ctx->as->plat->syshandler(ctx);
}
