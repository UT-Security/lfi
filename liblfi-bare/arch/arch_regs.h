#pragma once

#include "lfi_arch.h"
#include "lfi.h"

void regs_init(struct TuxRegs* regs, uintptr_t entry, uintptr_t sp);

uintptr_t* regs_return(struct TuxRegs* regs);

uintptr_t* regs_sp(struct TuxRegs* regs);
