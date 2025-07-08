#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#include "lfi.h"
#include "pal/platform.h"
#include "print.h"

EXPORT struct LFIAddrSpace*
lfi_as_new(struct LFIPlatform* plat)
{
    struct LFIAddrSpace* as = malloc(sizeof(struct LFIAddrSpace));
    if (!as)
        return NULL;

    void* mem = mmap(NULL, plat->opts.vmsize, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (mem == (void*)-1)
      goto err1;

    uintptr_t base = (uintptr_t)mem;

    *as = (struct LFIAddrSpace) {
        .base = base,
        .size = plat->opts.vmsize,
        .minaddr = base + plat->opts.pagesize,
        .maxaddr = base + plat->opts.vmsize,
        .plat = plat,
    };

    return as;

err1:
    free(as);
    return NULL;
}

EXPORT struct LFIAddrSpaceInfo
lfi_as_info(struct LFIAddrSpace* as)
{
    return (struct LFIAddrSpaceInfo) {
        .base = as->base,
        .size = as->size,
        .minaddr = as->minaddr,
        .maxaddr = as->maxaddr,
    };
}

EXPORT void
lfi_as_free(struct LFIAddrSpace* as)
{
    munmap((void*) as->base, as->size);
    free(as);
}
