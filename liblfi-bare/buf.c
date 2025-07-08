#include "buf.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

buf_t
bufreadfile(struct Tux* tux, const char* filename)
{
    int f = open(filename, O_RDONLY, 0);
    if (f < 0)
        return (buf_t) {NULL, 0};
    ssize_t size = lseek(f, 0, SEEK_END);
    if (size < 0)
        goto err;

    lseek(f, 0, SEEK_SET);
    void* p = mmap(NULL, size, PROT_READ, MAP_PRIVATE, f, 0);
    if (p == (void*) -1)
        goto err;

    close(f);
    return (buf_t) {
        .data = (uint8_t*) p,
        .size = size,
    };
err:
    close(f);
    return (buf_t){NULL, 0};
}
