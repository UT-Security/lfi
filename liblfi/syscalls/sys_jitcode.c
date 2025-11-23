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

//int sys_jitcode_create(struct TuxProc* p, lfiptr_t addrup, size_t length);

int sys_jitcode_create2(struct TuxProc* p, lfiptr_t addrp, lfiptr_t bufp1, size_t length1, lfiptr_t bufp2, size_t length2) {
  //TODO: overflow check
  size_t length = length1 + length2;

  uint8_t* buf1 = procbuf(p, bufp1, length1);
  uint8_t* buf2 = procbuf(p, bufp2, length2);

  uint8_t* buf = (uint8_t*)malloc(length);
  if(buf == NULL) {
    return -1;
  }

  memcpy(buf, buf1, length1);
  memcpy(buf + length1, buf2, length2);

  int r = proccreatejitcode(p, addrp, buf, length);
  free(buf);
  return r;
}

//int sys_jitcode_delete(struct TuxProc* p, lfiptr_t addrup, size_t length);

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
    return lfi_as_mprotect_no_verify(p->p_jit_as, addrp, length, LFI_PROT_EXEC);
}

int sys_jitcode_decommit(struct TuxProc* p, lfiptr_t addrp, size_t length) {
    if (!procjitvalid(p, addrp))
        return -1;
    LOCK_WITH_DEFER(&p->lk_as, lk_as);
    return lfi_as_mprotect_no_verify(p->p_jit_as, addrp, length, LFI_PROT_NONE);
}
