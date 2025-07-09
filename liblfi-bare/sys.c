#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <unistd.h>

#include "engine.h"
#include "lfi.h"
#include "print.h"
#include "types.h"
#include "sys.h"
#include "pal/platform.h"

#include "arch_sys.h"
#include "arch/arch_regs.h"

static inline int tuxerr(int err) {
  switch (err) {
  case ENOSYS:
    return -TUX_ENOSYS;
  case EINVAL:
    return -TUX_EINVAL;
  case ENOENT:
    return -TUX_ENOENT;
  case EBADF:
    return -TUX_EBADF;
  case EAGAIN:
    return -TUX_EAGAIN;
  case EPERM:
    return -TUX_EPERM;
  case ENOMEM:
    return -TUX_ENOMEM;
  case EACCES:
    return -TUX_EACCES;
  case ENOTDIR:
    return -TUX_ENOTDIR;
  case EMFILE:
    return -TUX_EMFILE;
  case EFAULT:
    return -TUX_EFAULT;
  default:
    return -TUX_EINVAL;
  }
}

#define SYS(SYSNO, expr)  \
    case TUX_SYS_##SYSNO: \
        r = expr;         \
        break;

uintptr_t
sys_exit(struct TuxThread* p, uint64_t code)
{
    VERBOSE(p->proc->tux, "sys_exit(%lx)", code);
    //clearctid(p);
    if (p->proc->tux->opts.pause_on_exit) {
        lfi_ctx_pause(p->p_ctx, code);
    } else {
        lfi_ctx_exit(p->p_ctx, code);
    }
    assert(!"unreachable");
}

uintptr_t
sys_exit_group(struct TuxThread* p, uint64_t code)
{
    VERBOSE(p->proc->tux, "sys_exit_group(%lx)", code);
    // TODO: exit all threads
    if (p->proc->tux->opts.pause_on_exit)
        lfi_ctx_pause(p->p_ctx, code);
    else
        lfi_ctx_exit(p->p_ctx, code);
    assert(!"unreachable");
}

uintptr_t sys_passthrough(struct TuxThread *t, uintptr_t sysno, uintptr_t a0,
                          uintptr_t a1, uintptr_t a2, uintptr_t a3,
                          uintptr_t a4, uintptr_t a5) {
  long r = syscall(sysno, a0, a1, a2, a3, a4, a5);
  if (r == -1)
    return tuxerr(r);
  VERBOSE(t->proc->tux,
          "passthrough: syscall %ld (%lx, %lx, %lx, %lx, %lx, %lx) = %ld",
          sysno, a0, a1, a2, a3, a4, a5, (long)r);
  return r;
}

static bool
isfork(uint64_t flags)
{
    uint64_t allowed = TUX_CLONE_CHILD_SETTID | TUX_CLONE_CHILD_CLEARTID;
    return (flags & ~allowed) == TUX_SIGCHLD ||
        (flags & ~allowed) == (TUX_CLONE_VM | TUX_CLONE_VFORK | TUX_SIGCHLD);
}

static void*
threadspawn(void* arg)
{
    struct TuxThread* p = (struct TuxThread*) arg;
    __asm__ __volatile__("wrgsbase %0" : : "r"(p->p_ctx->tp));
    lfi_tux_proc_run(p);
    VERBOSE(p->proc->tux, "thread %d exited", p->tid);
    return NULL;
}



static int
spawn(struct TuxThread* p, uint64_t flags, uint64_t stack, uint64_t ptidp, uint64_t ctidp, uint64_t tls, uint64_t func)
{
    if ((flags & 0xff) != 0 && (flags & 0xff) != TUX_SIGCHLD) {
        WARN(p->proc->tux, "unsupported clone signal: %x", (unsigned) flags & 0xff);
        return -TUX_EINVAL;
    }
    flags &= ~0xff;

    _Atomic(int)* ctid = (_Atomic(int)*) ctidp;
    _Atomic(int)* ptid = (_Atomic(int)*) ptidp;
    
    struct TuxThread* p2 = procnewthread(p);
    
    if (!p2) {
        return -TUX_EAGAIN;
    }

    if (flags & TUX_CLONE_SETTLS) {
        lfi_ctx_tpset(p2->p_ctx, tls);
    }
    if (flags & TUX_CLONE_CHILD_CLEARTID) {
        p2->ctid = ctidp;
    }
    if (flags & TUX_CLONE_CHILD_SETTID) {
        atomic_store_explicit(ctid, p2->tid, memory_order_release);
    }

    VERBOSE(p->proc->tux, "sys_clone(%lx, %lx, %lx, %lx, %lx) = %d", flags, stack, ptidp, ctidp, tls, p2->tid);

    struct TuxRegs* regs = lfi_ctx_regs(p2->p_ctx);
    *regs_return(regs) = 0;
    *regs_sp(regs) = stack;

    if (p->proc->tux->opts.libinit) {
        // A new thread is being created during sobox initialization. Instead
        // of creating a new kernel thread, we just save the stack and tls that
        // was created so it can be reused when we need to spawn threads in the
        // future.
        //struct LFIContext* save_ctx = lfi_myctx;
        //threadspawn(p2);
        //lfi_myctx = save_ctx;
        //pal_register_clonectx(p2->p_ctx);
    } else if (p->p_ctx == lfi_clonectx) {
        //struct LFIContext* save_ctx = lfi_myctx;
        //threadspawn(p2);
        //lfi_myctx = save_ctx;
        //lfi_newctx = p2->p_ctx;
    } else {
        // Actually create a new thread.
        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        int err = pthread_create(&thread, &attr, threadspawn, p2);
        pthread_attr_destroy(&attr);
        if (err) {
            assert(!"unimplemented: free machine");
            return -TUX_EAGAIN;
        }
    }

    if (flags & TUX_CLONE_PARENT_SETTID) {
        atomic_store_explicit(ptid, p2->tid, memory_order_release);
    }
    return p2->tid;
}

int
sys_clone(struct TuxThread* p, uint64_t flags, uint64_t stack, uint64_t ptid, uint64_t ctid, uint64_t tls, uint64_t func)
{
    if (isfork(flags)) {
        assert(!"unimplemented: fork or vfork");
    }
    return spawn(p, flags, stack, ptid, ctid, tls, func);
}

uintptr_t
syshandle(struct TuxThread* p, uintptr_t sysno, uintptr_t a0, uintptr_t a1,
        uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
    struct TuxProc* proc = p->proc;

    uintptr_t r = -TUX_ENOSYS;
    switch (sysno) {
    SYS(exit,              sys_exit(p, a0))
    SYS(exit_group,        sys_exit_group(p, a0))
# if defined(__x86_64__) || defined(_M_X64)
    // syscall: clone(flags, stack, ptid, ctid, tls, func)
    SYS(clone,             sys_clone(p, a0, a1, a2, a3, a4, a5))
# elif defined(__aarch64__) || defined(_M_ARM64)
    // syscall: clone(flags, stack, ptid, tls, ctid, func)
    // clone signature is different on aarch64 and x86-64
    SYS(clone,             sys_clone(p, a0, a1, a2, a4, a3, a5))
# endif
    default:
      //VERBOSE(p->proc->tux, "Unexpected pasthrough %ld", sysno);
      r = sys_passthrough(p, sysno, a0, a1, a2, a3, a4, a5);
    }

    return r;
}
