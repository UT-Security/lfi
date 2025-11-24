#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <stdatomic.h>
#include <syscall.h>
#include <sys/mman.h>

#include "arch_regs.h"
#include "cwalk.h"
#include "lfi.h"
#include "mmap.h"
#include "print.h"
#include "fd.h"
#include "buf.h"
#include "proc.h"
#include "elfload.h"
#include "pal/platform.h"
#include "pal/regs.h"

#include "syscalls/syscalls.h"
#include "types.h"

static bool procsetup(struct TuxThread* p, uint8_t* prog, size_t progsz, uint8_t* interp, size_t interpsz, int argc, char** argv);
static bool procfile(struct TuxThread* p, uint8_t* prog, size_t progsz, int argc, char** argv);
static void procfree(struct TuxThread*);

static int
sys_memfd_create(const char* name, unsigned flags) {
    return syscall(SYS_memfd_create, name, flags);
}

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
    p->proc->pid = nexttid();
    p->tid = p->proc->pid;
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
    p->proc->p_jit_as = NULL;
    p->p_ctx = ctx;

    if (!procfile(p, prog, size, argc, argv))
        goto err3;

    fdinit(tux, &p->proc->fdtable);

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
stacksetup(struct TuxProc* p, int argc, char** argv, struct LFILoadInfo* info, lfiptr_t* newsp)
{
    char* argv_ptrs[ARGC_MAX];
    char* stack_top = (char*) lfi_as_fmptr(p->p_as, info->stack) + info->stacksize;
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
    lfiptr_t* p_argc = (lfiptr_t*) (stack_top - 2 * ARG_BLOCK);
    *newsp = lfi_as_toptr(p->p_as, p_argc);
    *p_argc++ = argc;
    lfiptr_t* p_argvp = p_argc;
    lfiptr_t* p_argvp_start = p_argvp;
    for (int i = 0; i < argc; i++) {
        if ((uintptr_t) p_argvp >= (uintptr_t) stack_top - ARG_BLOCK) {
            return false;
        }
        p_argvp[i] = lfi_as_toptr(p->p_as, argv_ptrs[i]);
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

    lfiptr_t sp;
    if (!stacksetup(p->proc, argc, argv, &info, &sp))
        return false;

    lfiptr_t entry = info.elfentry;
    if (interp != NULL)
        entry = info.ldentry;

    struct TuxRegs* regs = lfi_ctx_regs(p->p_ctx);
    regs_init(regs, l2p(p->proc->p_as, entry), l2p(p->proc->p_as, sp));

    p->proc->brkbase = info.lastva;
    p->proc->brksize = 0;

    // Reserve the brk region.
    const int mapflags = LFI_MAP_PRIVATE | LFI_MAP_ANONYMOUS;
    lfiptr_t brkregion = lfi_as_mapat(p->proc->p_as, p->proc->brkbase, TUX_BRKMAXSIZE, LFI_PROT_NONE, mapflags, NULL, 0);
    if (brkregion == (lfiptr_t) -1)
        return false;

    return true;
}

struct TuxThread *
lfi_tux_get_thread(void)
{
    return (struct TuxThread *) lfi_get_myctx()->ctxp;
}

int
procmapany(struct TuxProc* p, size_t size, int prot, int flags, int fd,
        off_t offset, lfiptr_t* o_mapstart)
{
    struct HostFile* hf = NULL;
    if (fd >= 0) {
        // TODO: fdrelease
        struct FDFile* f = fdget(&p->fdtable, fd);
        if (!f)
            return -TUX_EBADF;
        if (f->file) {
            hf = f->file(f->dev);
        } else {
            return -TUX_EACCES;
        }
    }
    LOCK_WITH_DEFER(&p->lk_as, lk_as);
    lfiptr_t addr = lfi_as_mapany(p->p_as, size, prot, flags, hf, offset);
    if (addr == (lfiptr_t) -1)
        return -TUX_EINVAL;
    *o_mapstart = (uintptr_t) addr;
    return 0;
}

int
procmapat(struct TuxProc* p, lfiptr_t start, size_t size, int prot, int flags,
        int fd, off_t offset)
{
    struct HostFile* hf = NULL;
    if (fd >= 0) {
        // TODO: fdrelease
        struct FDFile* f = fdget(&p->fdtable, fd);
        if (!f)
            return -TUX_EBADF;
        if (f->file) {
            hf = f->file(f->dev);
        } else {
            return -TUX_EACCES;
        }
    }
    LOCK_WITH_DEFER(&p->lk_as, lk_as);
    if (procjitvalid(p, start))
        return -TUX_EINVAL;
    lfiptr_t addr = lfi_as_mapat(p->p_as, start, size, prot, flags, hf, offset);
    if (addr == (lfiptr_t) -1)
        return -TUX_EINVAL;
    return 0;
}

int
procunmap(struct TuxProc* p, lfiptr_t start, size_t size)
{
    LOCK_WITH_DEFER(&p->lk_as, lk_as);
    if (procjitvalid(p, start))
        return -TUX_EINVAL;
    return lfi_as_munmap(p->p_as, start, size);
}

static void
procfree(struct TuxThread* p)
{
    lfi_ctx_free(p->p_ctx);
    lfi_as_free(p->proc->p_as);
    free(p->proc);
    free(p);
}

int procmapjitcode(struct TuxProc* p, size_t exec_size, size_t data_size, lfiptr_t* o_mapstart) {
    LOCK_WITH_DEFER(&p->lk_jit_as, lk_jit_as);
    if (p->p_jit_as != NULL) {
        return -TUX_EINVAL;
    }

    int fd = sys_memfd_create("", 0);
    if (fd < 0) {
        return -TUX_EINVAL;
    }

    //TODO: overflow check
    size_t size = exec_size + data_size;
    int r = ftruncate(fd, size);
    if (r < 0) {
        close(fd);
        return -TUX_EINVAL;
    }

    void* aliasmap = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (aliasmap == (void*) -1) {
        close(fd);
        return -TUX_EINVAL;
    }

    struct HostFile* hf = lfi_host_fdopen(fd);
    if (!hf) {
        close(fd);
        munmap(aliasmap, size);
        return -TUX_EINVAL;
    }

    LOCK_WITH_DEFER(&p->lk_as, lk_as);
    lfiptr_t addr = lfi_as_mapany(p->p_as, size, PROT_NONE, MAP_SHARED, hf, 0);
    if (addr == (lfiptr_t) -1) {
        close(fd);
        munmap(aliasmap, size);
        return -TUX_EINVAL;
    }

    struct LFIAddrSpace* jit_as = malloc(sizeof(struct LFIAddrSpace));
    if (!jit_as) {
        close(fd);
        munmap(aliasmap, size);
        return -TUX_EINVAL;
    }

    *jit_as = (struct LFIAddrSpace) {
        .base = addr,
        .size = size,
        .minaddr = addr,
        .maxaddr = addr + exec_size,
        .plat = p->p_as->plat,
    };

    bool ok = mm_init(&jit_as->mm, jit_as->minaddr,
                      jit_as->maxaddr - jit_as->minaddr, 32);
    if (!ok) {
        free(jit_as);
        close(fd);
        munmap(aliasmap, size);
        return -TUX_EINVAL; 
    }

    p->p_jit_as = jit_as;
    p->p_jit_info = lfi_as_info(jit_as);
    p->jit_fd = fd;
    p->jit_alias = (uint8_t*)aliasmap;
    
    *o_mapstart = (uintptr_t) addr;
    return 0;
}

int
procunmapjitcode(struct TuxProc* p, lfiptr_t start, size_t exec_size, size_t data_size)
{
    LOCK_WITH_DEFER(&p->lk_jit_as, lk_jit_as);
    if (p->p_jit_as == NULL) {
        return -TUX_EINVAL;
    }

    size_t size = exec_size + data_size;

    if (p->p_jit_as->base != start || p->p_jit_as->size != size ||
        p->p_jit_as->maxaddr != start + exec_size) {
        return -TUX_EINVAL;
    }

    //TODO: maybe check that there are not live jit allocations.

    struct LFIAddrSpace* jit_as = p->p_jit_as;
    int jit_fd = p->jit_fd;
    uint8_t* jit_alias = p->jit_alias;

    p->p_jit_as = NULL;
    p->jit_fd = -1;
    p->jit_alias = NULL;

    free(jit_as);

    LOCK_WITH_DEFER(&p->lk_as, lk_as);
    if(lfi_as_munmap(p->p_as, start, size) == -1) {
        return -TUX_EINVAL;
    }

    if (munmap(jit_alias, size) != 0) {
        return -TUX_EINVAL;
    }

    close(jit_fd);
    return 0;
}

static void
cbunmap_exec(uint64_t start, size_t len, MMInfo info, void* udata)
{
    (void) info;
    struct TuxProc* p = (struct TuxProc*)udata;
    memset(procjitcodeaddr(p, start), 0xcc, len);
}

int proccreatejitcode(struct TuxProc* p, lfiptr_t dst, uint8_t* src, size_t size) {
    LOCK_WITH_DEFER(&p->lk_jit_as, lk_jit_as);
    if (p->p_jit_as == NULL) {
        return -TUX_EINVAL;
    }

    if (!lfi_as_validptr(p->p_jit_as, dst) || !lfi_as_validptr(p->p_jit_as, dst + size)) {
        return -TUX_EINVAL;
    }

    LOCK_WITH_DEFER(&p->lk_as, lk_as);
    //MMInfo info;
    //if(!mm_querypage(&p->p_as->mm, dst, &info)) {
    //    return -TUX_EINVAL;
    //}

    //TODO: maybe sanity expect the allocation to be currently READ | EXEC

    //lfi_as_mprotect_no_verify(p->p_as, info.base, info.len, LFI_PROT_NONE);

    uintptr_t m_addr = mm_mapat_cb(
        &p->p_jit_as->mm, l2p(p->p_jit_as, dst), size, LFI_PROT_READ,
        LFI_MAP_FIXED | LFI_MAP_PRIVATE, NULL, 0, cbunmap_exec, p);
    if (m_addr == (uintptr_t) -1) {
        return -TUX_EINVAL;
    }

    memcpy(procjitcodeaddr(p, dst), src, size);
    //TODO: call verifier on memcpyd range

    ///lfi_as_mprotect_no_verify(p->p_as, info.base, info.len, LFI_PROT_EXEC | LFI_PROT_READ);
    return 0;
}

int procdeletejitcode(struct TuxProc* p, lfiptr_t dst, size_t length) {
    LOCK_WITH_DEFER(&p->lk_jit_as, lk_jit_as);
    if (p->p_jit_as == NULL) {
        return -TUX_EINVAL;
    }

    if (!lfi_as_validptr(p->p_jit_as, dst) || !lfi_as_validptr(p->p_jit_as, dst + length)) {
        return -TUX_EINVAL;
    }

    if (l2p(p->p_jit_as, dst) >= p->p_jit_as->minaddr && l2p(p->p_jit_as, dst) + length < p->p_jit_as->maxaddr)
        return mm_unmap_cb(&p->p_jit_as->mm, l2p(p->p_jit_as, dst), length, NULL, NULL);

    //TODO: clear out jitcode in executable memory

    return -TUX_EINVAL;
}

EXPORT struct TuxThread*
lfi_tux_proc_new(struct Tux* tux, uint8_t* prog, size_t progsz, int argc, char** argv)
{
    return procnewfile(tux, prog, progsz, argc, argv);
}

EXPORT bool
lfi_proc_init(struct LFIContext* ctx, struct LFIAddrSpace* as, struct LFILoadInfo info)
{
    regs_init(lfi_ctx_regs(ctx), info.ldentry ? l2p(as, info.ldentry) : l2p(as, info.elfentry), l2p(as, info.stack + info.stacksize - 16));
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
