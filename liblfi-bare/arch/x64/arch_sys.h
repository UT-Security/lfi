#pragma once

#include "lfi.h"

enum {
    TUX_SYS_open               = 2,

    TUX_SYS_clone              = 56,

    TUX_SYS_exit               = 60,

    TUX_SYS_prctl              = 157,
    TUX_SYS_arch_prctl         = 158,

    TUX_SYS_exit_group         = 231,

    // always last syscall+1
    TUX_SYS_ntotal             = 335,
};
void arch_syshandle(struct LFIContext* ctx);
