#define _GNU_SOURCE

#include <assert.h>
#include <stdalign.h>
#include <signal.h>

#include "syscalls/syscalls.h"

struct SignalFrame {
    uint8_t ret[8];
    struct SigInfo si;
    struct UContext uc;
    struct FPState fp;
};

static void
put64(uint8_t *p, uint64_t v)
{
    __builtin_memcpy(p, &v, 8);
}

static void
put32(uint8_t *p, uint32_t v)
{
    __builtin_memcpy(p, &v, 4);
}

static const int kRedzoneSize = 128;

#define ROUNDDOWN(X, K) ((X) & -(K))

bool
lfi_tux_on_signal(struct TuxThread *p, int sig, int code, siginfo_t *si, void *ucontext)
{
    if (!p->proc->signals[sig].valid)
        return false;

    struct SigAction sighand = p->proc->signals[sig].entry;

    struct SignalFrame sf = {0};

    ucontext_t *ctx = (ucontext_t *)ucontext;
    greg_t *host_regs = ctx->uc_mcontext.gregs;

    put32(sf.si.signo, sig);
    put32(sf.si.code, code);

    if (sig == LINUX_SIGILL ||
        sig == LINUX_SIGFPE ||
        sig == LINUX_SIGSEGV ||
        sig == LINUX_SIGBUS ||
        sig == LINUX_SIGTRAP) {
        put64(sf.si.addr, (uintptr_t) si->si_addr);
    }

    // skip: sigmask
    put64(sf.uc.r8, host_regs[REG_R8]);
    put64(sf.uc.r9, host_regs[REG_R9]);
    put64(sf.uc.r10, host_regs[REG_R10]);
    put64(sf.uc.r11, host_regs[REG_R11]);
    put64(sf.uc.r12, host_regs[REG_R12]);
    put64(sf.uc.r13, host_regs[REG_R13]);
    put64(sf.uc.r14, host_regs[REG_R14]);
    put64(sf.uc.r15, host_regs[REG_R15]);
    put64(sf.uc.rdi, host_regs[REG_RDI]);
    put64(sf.uc.rsi, host_regs[REG_RSI]);
    put64(sf.uc.rbp, host_regs[REG_RBP]);
    put64(sf.uc.rbx, host_regs[REG_RBX]);
    put64(sf.uc.rdx, host_regs[REG_RDX]);
    put64(sf.uc.rax, host_regs[REG_RAX]);
    put64(sf.uc.rcx, host_regs[REG_RCX]);
    put64(sf.uc.rsp, host_regs[REG_RSP]);
    put64(sf.uc.rip, host_regs[REG_RIP]);
    put64(sf.uc.eflags, host_regs[REG_EFL]);
    // skip: X87 state

    struct TuxRegs *regs = lfi_ctx_regs(p->p_ctx);
    uintptr_t sp = regs->rsp;
    sp -= kRedzoneSize;

    sp = ROUNDDOWN(sp, 16);
    sp -= sizeof(sf);
    assert((sp & 15) == 8);

    put64(sf.ret, sighand.restorer);
    put64(sf.uc.fpstate, sp + offsetof(struct SignalFrame, fp));

    regs->rsp = sp;
    regs->rdi = sig;
    regs->rsi = sp + offsetof(struct SignalFrame, si);
    regs->rdx = sp + offsetof(struct SignalFrame, uc);

    // TODO: set pc to sighand.handler
    assert(!"unimplemented: jump to sandbox signal handler");
}

int
sys_rt_sigaction(struct TuxProc* p, int sig, int64_t act, int64_t old, uint64_t sigsetsize)
{
    if (sig < 1 || sig >= LINUX_NSIG)
        return -TUX_EINVAL;

    if (sigsetsize != 8) {
        WARN(p->tux, "sigsetsize != 8");
        return -TUX_EINVAL;
    }

    uint8_t* ab = procbufalign(p, act, sizeof(struct SigAction), alignof(struct SigAction));
    if (!ab)
        return -TUX_EFAULT;

    struct SigAction* tux_act = (struct SigAction*) ab;

    if (p->signals[sig].valid) {
        WARN(p->tux, "TODO: rt_sigaction should set oldact");
        return -TUX_EINVAL;
    }

    if ((tux_act->flags & SA_RESTORER) == 0) {
        WARN(p->tux, "TODO: rt_sigaction must use SA_RESTORER");
        return -TUX_EINVAL;
    }

    p->signals[sig].entry = *tux_act;
    p->signals[sig].valid = true;

    WARN(p->tux, "rt_sigaction (%d): handler: %lx", sig, tux_act->handler);
    WARN(p->tux, "sa_flags: %lx", tux_act->flags);
    return 0;
}

int
sys_rt_sigprocmask(struct TuxProc* p, int how, int64_t setaddr, int64_t oldsetaddr, uint64_t sigsetsize)
{
    WARN(p->tux, "unimplemented: rt_sigprocmask");
    return 0;
}

int
sys_rt_sigreturn(struct TuxProc* p)
{
    assert(!"unimplemented: rt_sigreturn");
}
