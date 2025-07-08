#pragma once

#include <assert.h>

#include "lfi.h"
#include "lfi_arch.h"
#include "types.h"

struct LFIPlatform {
    struct LFIPlatOptions opts;
    SysHandlerFn syshandler;
};

struct LFIAddrSpace {
    uintptr_t base;
    size_t size;
    uintptr_t minaddr;
    uintptr_t maxaddr;

    struct LFIPlatform* plat;
};

struct Sys {
    uintptr_t rtcalls[256];
    uintptr_t base;
    uintptr_t ctxp;
};

struct ElfTable {
    char* tab;
    size_t size;
};

struct LFIContext {
    void* kstackp;
    uintptr_t tp;
    uintptr_t ktpderef;
    uintptr_t _pad;
    struct TuxRegs regs;
    void* ctxp;
    struct Sys* sys;
    struct LFIAddrSpace* as;

    uintptr_t elfbase;
    struct ElfTable symtab;
    struct ElfTable strtab;
};

static inline size_t
gb(size_t x)
{
    return x * 1024 * 1024 * 1024;
}

static inline size_t
kb(size_t x)
{
    return x * 1024;
}

_Thread_local extern struct LFIContext* lfi_myctx;

_Thread_local extern struct LFIContext* lfi_newctx;

extern struct LFIContext* lfi_clonectx;

void pal_register_clonectx(struct LFIContext* ctx);
