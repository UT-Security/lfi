#include <assert.h>
#include <unistd.h>
#include <stdlib.h>

#include "lfi.h"
#include "pal/platform.h"

#define asm __asm__

static void showerr(char* msg, size_t sz) {
    (void) sz;
    fprintf(stderr, "%s\n", msg);
}

EXPORT struct LFIPlatform*
lfi_new_plat(struct LFIPlatOptions opts)
{
    struct LFIPlatform* plat = malloc(sizeof(struct LFIPlatform));
    if (!plat)
        goto err1;

    struct BoxMap* bm = boxmap_new((struct BoxMapOptions) {
        .minalign = gb(512),
        .maxalign = gb(512),
        .guardsize = gb(0),
    });
    if (!bm)
        goto err2;
    if (!boxmap_reserve(bm, gb(1024)))
        goto err3;

    *plat = (struct LFIPlatform) {
        .bm = bm,
        .opts = opts,
        .verifier = NULL,
    };
    return plat;

err3:
    boxmap_delete(bm);
err2:
    free(plat);
err1:
    return NULL;
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
