#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <pthread.h>

#include "lfi_tux.h"
#include "lfi.h"

struct TuxProc {
    struct LFIAddrSpace* p_as;

    struct Tux* tux;
    struct LFIAddrSpaceInfo p_info;
};

struct TuxThread {
    struct LFIContext* p_ctx;
    uintptr_t stack;

    uintptr_t ctid;
    int tid;

    struct TuxProc* proc;
};

struct TuxThread* procnewthread(struct TuxThread* p);
