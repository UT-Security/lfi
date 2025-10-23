#define _GNU_SOURCE
#include "lfi.h"

#include <assert.h>
#include <stdalign.h>
#include <signal.h>

#include "pal/platform.h"
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

static uint64_t
read64(uint8_t *p)
{
    uint64_t v;
    __builtin_memcpy(&v, p, 8);
    return v;
}

static void
put32(uint8_t *p, uint32_t v)
{
    __builtin_memcpy(p, &v, 4);
}

static const int kRedzoneSize = 128;

#define ROUNDDOWN(X, K) ((X) & -(K))

extern uint64_t lfi_ctx_entry(struct LFIContext* ctx, void** kstackp)
    asm ("lfi_ctx_entry");

bool
lfi_tux_on_signal(struct TuxThread *p, int sig, siginfo_t *si, void *ucontext)
{
    if (!p->proc->signals[sig].valid)
        return false;

    struct SigAction sighand = p->proc->signals[sig].entry;

    struct SignalFrame sf = {0};

    ucontext_t *ctx = (ucontext_t *)ucontext;
    greg_t *host_regs = ctx->uc_mcontext.gregs;

    put32(sf.si.signo, sig);
    put32(sf.si.code, si->si_code);

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
    //TODO: make sure sp is valid/sandboxed.
    uintptr_t sp = host_regs[REG_RSP];
    sp -= kRedzoneSize;

    sp = ROUNDDOWN(sp, 16);
    sp -= sizeof(sf);
    assert((sp & 15) == 8);

    //WARN(p->proc->tux, "restorer: %lx", sighand.restorer);
    put64(sf.ret, sighand.restorer);
    put64(sf.uc.fpstate, sp + offsetof(struct SignalFrame, fp));

    memcpy((void *) sp, &sf, sizeof(sf));

    regs->rsp = sp;
    regs->rdi = sig;
    regs->rsi = sp + offsetof(struct SignalFrame, si);
    regs->rdx = sp + offsetof(struct SignalFrame, uc);

    void *saved_sp = p->p_ctx->kstackp;

    regs->r11 = sighand.handler;
    lfi_ctx_run(p->p_ctx, p->proc->p_as);

    p->p_ctx->kstackp = saved_sp;

    // restore
    regs = lfi_ctx_regs(p->p_ctx);
    sp = regs->rsp;
    memcpy(&sf, (void *) (sp - 8), sizeof(sf));

    ucontext_t *uc = (ucontext_t *) ucontext;

    uintptr_t requested_rip = read64(sf.uc.rip);
    if (!lfi_as_validptr(p->proc->p_as, requested_rip)) {
        printf("on_signal: requested RIP is invalid!\n");
        return false;
    }
    if ((requested_rip & 0x1f) != 0) {
        printf("on_signal: requested RIP is not bundle-aligned!\n");
        return false;
    }
    uc->uc_mcontext.gregs[REG_RIP] = requested_rip;

    regs->r8 = host_regs[REG_R8];
    regs->r9 = host_regs[REG_R9];
    regs->r10 = host_regs[REG_R10];
    regs->r11 = host_regs[REG_R11];
    regs->r12 = host_regs[REG_R12];
    regs->r13 = host_regs[REG_R13];
    regs->r14 = host_regs[REG_R14];
    regs->r15 = host_regs[REG_R15];
    regs->rdi = host_regs[REG_RDI];
    regs->rsi = host_regs[REG_RSI];
    regs->rbp = host_regs[REG_RBP];
    regs->rbx = host_regs[REG_RBX];
    regs->rdx = host_regs[REG_RDX];
    regs->rax = host_regs[REG_RAX];
    regs->rcx = host_regs[REG_RCX];
    regs->rsp = host_regs[REG_RSP];

    return true;
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

    uint8_t* ob = procbufalign(p, old, sizeof(struct SigAction), alignof(struct SigAction));
    if (ob) {
        struct SigAction* tux_old = (struct SigAction*) ob;

        if (p->signals[sig].valid) {
            WARN(p->tux, "TODO: rt_sigaction should set oldact");
            return -TUX_EINVAL;
        } else {
            tux_old->handler = LINUX_SIG_DFL;
        }
    }

    if (tux_act->handler == LINUX_SIG_DFL || tux_act->handler == LINUX_SIG_IGN) {
        p->signals[sig].valid = false;
        return 0;
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
sys_rt_sigreturn(struct TuxThread* p)
{
    WARN(p->proc->tux, "rt_sigreturn");
    lfi_ctx_exit(p->p_ctx, 0);
    assert(!"sigreturn: unreachable");
}
