#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "lfi_arch.h"

#ifdef __cplusplus
extern "C" {
#endif

struct LFIPlatform;

struct LFIAddrSpace;

struct LFIContext;

struct LFIAddrSpaceInfo {
    uintptr_t base;
    size_t size;
    
    uintptr_t minaddr;
    uintptr_t maxaddr;
};

struct LFILoadInfo {
    uintptr_t stack;
    size_t stacksize;
    uintptr_t lastva;
    uintptr_t elfentry;
    uintptr_t ldentry;
    uintptr_t elfbase;
    uintptr_t ldbase;
    uint64_t elfphoff;
    uint16_t elfphnum;
    uint16_t elfphentsize;
};

struct LFIPlatOptions {
    size_t pagesize;
    size_t vmsize;
};

struct LFIPlatform* lfi_new_plat(struct LFIPlatOptions opts);

typedef void (*SysHandlerFn)(struct LFIContext* ctx);

struct LFIAddrSpace*    lfi_as_new(struct LFIPlatform* plat);
struct LFIAddrSpaceInfo lfi_as_info(struct LFIAddrSpace* as);
void                    lfi_as_free(struct LFIAddrSpace* as);

struct LFIContext*      lfi_ctx_new(struct LFIAddrSpace* as, void* ctxp, bool mainthread);
uint64_t                lfi_ctx_run(struct LFIContext* ctx, struct LFIAddrSpace* as);
void*                   lfi_ctx_data(struct LFIContext* ctx);
void                    lfi_ctx_free(struct LFIContext* ctx);
struct TuxRegs*         lfi_ctx_regs(struct LFIContext* ctx);
void                    lfi_ctx_exit(struct LFIContext* ctx, uint64_t val);
void                    lfi_ctx_pause(struct LFIContext* ctx, uint64_t val);
void                    lfi_ctx_tpset(struct LFIContext* ctx, uintptr_t tp);
struct LFIAddrSpace*    lfi_ctx_as(struct LFIContext* ctx);
struct LFIContext*      lfi_get_myctx(void);

void                    lfi_sys_handler(struct LFIPlatform* plat, SysHandlerFn fn);

struct LFILoadOpts {
    size_t stacksize;
    size_t pagesize;
};

bool                    lfi_proc_loadelf(struct LFIAddrSpace* as, uint8_t* prog, size_t progsz, uint8_t* interp, size_t interpsz, struct LFILoadInfo* o_info, struct LFILoadOpts opts);
bool                    lfi_proc_init(struct LFIContext* ctx, struct LFIAddrSpace* as, struct LFILoadInfo info);
bool                    lfi_proc_loadsyms(struct LFIContext* ctx, uint8_t* elfdat, size_t elfsize);
uint64_t                lfi_proc_sym(struct LFIContext* ctx, char* sym);

void lfi_thread_init(void (*thread_create)(void*), void* pausefn);

char* lfi_strerror(void);

#ifdef __cplusplus
}
#endif
