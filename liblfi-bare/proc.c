#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <stdatomic.h>

#include "arch_regs.h"
#include "cwalk.h"
#include "buf.h"
#include "engine.h"
#include "print.h"
#include "proc.h"
#include "elfload.h"
#include "pal/platform.h"
#include "pal/regs.h"

static bool procsetup(struct TuxThread* p, uint8_t* prog, size_t progsz, uint8_t* interp, size_t interpsz, int argc, char** argv);
static bool procfile(struct TuxThread* p, uint8_t* prog, size_t progsz, int argc, char** argv);
static void procfree(struct TuxThread*);

static int
nexttid(void)
{
    // TODO: set a limit on the number of threads?
    static _Atomic(int) tid = 10000;
    return atomic_fetch_add_explicit(&tid, 1, memory_order_relaxed);
}

static struct TuxThread*
procnewempty(void)
{
    struct TuxProc* proc = calloc(sizeof(struct TuxProc), 1);
    if (!proc)
        return NULL;
    struct TuxThread* p = calloc(sizeof(struct TuxThread), 1);
    if (!p)
        goto err;
    p->proc = proc;
    p->tid = nexttid();
    return p;

err:
    free(proc);
    return NULL;
}

struct TuxThread*
procnewthread(struct TuxThread* p)
{
    struct TuxThread* newp = calloc(sizeof(struct TuxThread), 1);
    if (!newp)
        goto err;

    struct LFIContext* ctx = lfi_ctx_new(p->proc->p_as, newp, false);
    if (!ctx)
        goto err1;
    newp->p_ctx = ctx;
    newp->proc = p->proc;
    newp->tid = nexttid();
    *lfi_ctx_regs(newp->p_ctx) = *lfi_ctx_regs(p->p_ctx);
    lfi_ctx_init_sys(newp->p_ctx);

    return newp;

err1:
    free(newp);
err:
    return NULL;
}

static struct TuxThread*
procnewfile(struct Tux* tux, uint8_t* prog, size_t size, int argc, char** argv)
{
    struct TuxThread* p = procnewempty();
    if (!p)
        return NULL;
    p->proc->tux = tux;
    struct LFIAddrSpace* as = lfi_as_new(tux->plat);
    if (!as)
        goto err1;
    struct LFIContext* ctx = lfi_ctx_new(as, p, true);
    if (!ctx)
        goto err2;
    p->proc->p_as = as;
    p->proc->p_info = lfi_as_info(as);
    p->p_ctx = ctx;

    if (!procfile(p, prog, size, argc, argv))
        goto err3;

    return p;
err3:
err2:
err1:
    procfree(p);
    return NULL;
}

static bool
procfile(struct TuxThread* p, uint8_t* prog, size_t progsz, int argc, char** argv)
{
    char* interppath = elfinterp(prog, progsz);
    buf_t interp = (buf_t){NULL, 0};
    if (interppath) {
        if (cwk_path_is_absolute(interppath)) {
            interp = bufreadfile(p->proc->tux, interppath);
            if (!interp.data) {
                WARN(p->proc->tux, "error opening dynamic linker %s: %s", interppath, strerror(errno));
                free(interppath);
                return false;
            }
            VERBOSE(p->proc->tux, "dynamic linker: %s", interppath);
        } else {
            WARN(p->proc->tux, "interpreter ignored because it is relative path: %s", interppath);
        }
        free(interppath);
    }

    bool success = true;
    if (!procsetup(p, prog, progsz, interp.data, interp.size, argc, argv))
        success = false;

    return success;
}

enum {
    ARGC_MAX = 1024,
    ARGV_MAX = 1024,

    ARG_BLOCK = 4096,
};

static bool
stacksetup(struct TuxProc* p, int argc, char** argv, struct LFILoadInfo* info, uintptr_t* newsp)
{
    char* argv_ptrs[ARGC_MAX];
    char* stack_top = (char*) info->stack + info->stacksize;
    char* p_argv = (char*) stack_top - ARG_BLOCK;

    // Write argv string values to the stack.
    for (int i = 0; i < argc; i++) {
        size_t len = strnlen(argv[i], ARGV_MAX) + 1;

        if (p_argv + len >= stack_top) {
            return false;
        }

        memcpy(p_argv, argv[i], len);
        p_argv[len - 1] = 0;
        argv_ptrs[i] = p_argv;
        p_argv += len;
    }

    // Write argc and argv pointers to the stack.
    uintptr_t* p_argc = (uintptr_t*) (stack_top - 2 * ARG_BLOCK);
    *newsp = (uintptr_t)p_argc;
    *p_argc++ = argc;
    uintptr_t* p_argvp = p_argc;
    uintptr_t* p_argvp_start = p_argvp;
    for (int i = 0; i < argc; i++) {
        if ((uintptr_t) p_argvp >= (uintptr_t) stack_top - ARG_BLOCK) {
            return false;
        }
        p_argvp[i] = (uintptr_t)argv_ptrs[i];
    }
    p_argvp[argc] = 0;
    // Empty envp.
    char** p_envp = (char**) &p_argvp[argc + 1];
    *p_envp++ = NULL;

    struct Auxv* av = (struct Auxv*) p_envp;
    *av++ = (struct Auxv) { AT_SECURE, 0 };
    *av++ = (struct Auxv) { AT_BASE, info->ldbase };
    *av++ = (struct Auxv) { AT_PHDR, info->elfbase + info->elfphoff };
    *av++ = (struct Auxv) { AT_PHNUM, info->elfphnum };
    *av++ = (struct Auxv) { AT_PHENT, info->elfphentsize };
    *av++ = (struct Auxv) { AT_ENTRY, info->elfentry };
    *av++ = (struct Auxv) { AT_EXECFN, p_argvp_start[0] };
    *av++ = (struct Auxv) { AT_PAGESZ, p->tux->opts.pagesize };
    *av++ = (struct Auxv) { AT_HWCAP, 0 };
    *av++ = (struct Auxv) { AT_HWCAP2, 0 };
    *av++ = (struct Auxv) { AT_RANDOM, p_argvp_start[0] }; // TODO: AT_RANDOM
    *av++ = (struct Auxv) { AT_FLAGS, 0 };
    *av++ = (struct Auxv) { AT_UID, 1000 };
    *av++ = (struct Auxv) { AT_EUID, 1000 };
    *av++ = (struct Auxv) { AT_GID, 1000 };
    *av++ = (struct Auxv) { AT_EGID, 1000 };
    *av++ = (struct Auxv) { AT_SYSINFO, 0 };
    *av++ = (struct Auxv) { AT_SYSINFO_EHDR, 0 };
    *av++ = (struct Auxv) { AT_NULL, 0 };

    return true;
}

static bool
procsetup(struct TuxThread* p, uint8_t* prog, size_t progsz, uint8_t* interp, size_t interpsz, int argc, char** argv)
{
    struct LFILoadInfo info = {0};
    bool b = elfload(p, prog, progsz, interp, interpsz, &info);
    if (!b)
        return false;

    uintptr_t sp;
    if (!stacksetup(p->proc, argc, argv, &info, &sp))
        return false;

    uintptr_t entry = info.elfentry;
    if (interp != NULL)
        entry = info.ldentry;

    struct TuxRegs* regs = lfi_ctx_regs(p->p_ctx);
    regs_init(regs, entry, sp);

    return true;
}

static void
procfree(struct TuxThread* p)
{
    lfi_ctx_free(p->p_ctx);
    lfi_as_free(p->proc->p_as);
    free(p->proc);
    free(p);
}

EXPORT struct TuxThread*
lfi_tux_proc_new(struct Tux* tux, uint8_t* prog, size_t progsz, int argc, char** argv)
{
    return procnewfile(tux, prog, progsz, argc, argv);
}

EXPORT bool
lfi_proc_init(struct LFIContext* ctx, struct LFIAddrSpace* as, struct LFILoadInfo info)
{
    regs_init(lfi_ctx_regs(ctx), info.ldentry ? info.ldentry : info.elfentry, info.stack + info.stacksize - 16);
    return true;
}

EXPORT struct LFIContext*
lfi_tux_ctx(struct TuxThread* p)
{
    return p->p_ctx;
}

EXPORT void
lfi_tux_proc_free(struct TuxThread* p)
{
    procfree(p);
}

EXPORT uintptr_t
lfi_tux_proc_stack(struct TuxThread* p)
{
    return p->stack;
}
