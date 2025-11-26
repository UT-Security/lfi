#include <assert.h>
#include <stdlib.h>

#include "align.h"
#include "lfi.h"
#include "syscalls/syscalls.h"

uintptr_t sys_jitcode_mmap(struct TuxProc* p, lfiptr_t addrp, size_t exec_length, size_t data_length) {
    if (exec_length == 0 || exec_length != ceilp(exec_length, p->tux->opts.pagesize))
        return -TUX_EINVAL;
    if (data_length != 0 && data_length != ceilp(data_length, p->tux->opts.pagesize))
        return -TUX_EINVAL;

    lfiptr_t i_addrp = addrp;
    int r = procmapjitcode(p, exec_length, data_length, &addrp);
    if (r < 0) {
        VERBOSE(p->tux, "sys_jitcode_mmap((%lx), %ld, %ld) = %d", i_addrp, exec_length, data_length, r);
        return r;
    }
    lfiptr_t ret = addrp;
    VERBOSE(p->tux, "sys_jitcode_mmap(%lx (%lx), %ld, %ld) = %lx", addrp, i_addrp, exec_length, data_length, ret);
    return ret;
}

int sys_jitcode_create(struct TuxProc* p, lfiptr_t addrp, lfiptr_t bufp, size_t length) {
  // Make sure addresses are bundle aligned
  int bundle_size = 32;
  if(addrp % bundle_size != 0) {
      VERBOSE(p->tux, "sys_jitcode_create: addr not bundle aligned!");
      return -1;
  }
  // TODO: pad length up to bundle size
  uint8_t* src = procbuf(p, bufp, length);
  int r = proccreatejitcode(p, addrp, src, length);
  return r;
}

int sys_jitcode_create2(struct TuxProc* p, lfiptr_t addrp, lfiptr_t bufp, size_t total_length,
                        size_t header_length) {
  // TODO: We probably need better sanity checks here
  assert(total_length >= header_length);
  // Make sure addresses are bundle aligned
  int bundle_size = 32;
  if(addrp % bundle_size != 0) {
      VERBOSE(p->tux, "sys_jitcode_create: addr not bundle aligned!");
      return -1;
  }
  // TODO: pad length up to bundle size
  uint8_t* src = procbuf(p, bufp, total_length);
  uint8_t* buf = (uint8_t*)malloc(total_length);
  if(buf == NULL) {
    return -1;
  }

  memcpy(buf, src + total_length - header_length, header_length);
  memcpy(buf + header_length, src, total_length - header_length);

  int r = proccreatejitcode(p, addrp, buf, total_length);
  free(buf);
  return r;
}

int sys_jitcode_modify(struct TuxProc* p, lfiptr_t addrp, size_t valp, size_t length) {
    // Make sure you can only modify upto a 5-byte nop/call/jmp
    if(length > 5) {
        return -1;
    }
    int r = procmodifyjitcode(p, addrp, valp, length);
    return r;
}


int sys_jitcode_delete(struct TuxProc* p, lfiptr_t addrp, size_t length) {
    if (!procjitvalid(p, addrp))
        return -1;

    return procdeletejitcode(p, addrp, length);
  
}

int sys_jitcode_munmap(struct TuxProc* p, lfiptr_t addrp, size_t exec_length, size_t data_length) {
    if (exec_length == 0 || exec_length != ceilp(exec_length, p->tux->opts.pagesize))
        return -TUX_EINVAL;
    if (data_length != 0 && data_length != ceilp(data_length, p->tux->opts.pagesize))
        return -TUX_EINVAL;
  
    if (!procjitvalid(p, addrp))
        return -1;

    return procunmapjitcode(p, addrp, exec_length, data_length);
}

int sys_jitcode_commit(struct TuxProc* p, lfiptr_t addrp, size_t length) {
    if (!procjitvalid(p, addrp))
        return -1;
    LOCK_WITH_DEFER(&p->lk_as, lk_as);
    //TODO: clear out page to be safe
    return lfi_as_mprotect_no_verify(p->p_jit_as, addrp, length, LFI_PROT_READ | LFI_PROT_EXEC);
}

int sys_jitcode_decommit(struct TuxProc* p, lfiptr_t addrp, size_t length) {
    if (!procjitvalid(p, addrp))
        return -1;
    LOCK_WITH_DEFER(&p->lk_as, lk_as);
    return lfi_as_mprotect_no_verify(p->p_jit_as, addrp, length, LFI_PROT_NONE);
}
