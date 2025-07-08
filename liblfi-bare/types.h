#pragma once

#include <stdint.h>

#define EXPORT __attribute__((visibility("default")))

typedef uint64_t  u64;
typedef uint32_t  u32;
typedef uint16_t  u16;
typedef uint8_t   u8;
typedef int64_t   i64;
typedef int32_t   i32;
typedef int16_t   i16;
typedef int8_t    i8;

enum {
    TUX_EPERM   = 1,
    TUX_ENOENT  = 2,
    TUX_EBADF   = 9,
    TUX_EAGAIN  = 11,
    TUX_ENOMEM  = 12,
    TUX_EACCES  = 13,
    TUX_EFAULT  = 14,
    TUX_ENOTDIR = 20,
    TUX_EINVAL  = 22,
    TUX_EMFILE  = 24,
    TUX_ENOSYS  = 38,
};

enum {
    TUX_CLONE_VM             = 0x100,
    TUX_CLONE_FS             = 0x200,
    TUX_CLONE_FILES          = 0x400,
    TUX_CLONE_SIGHAND        = 0x800,
    TUX_CLONE_VFORK          = 0x4000,
    TUX_CLONE_THREAD         = 0x10000,
    TUX_CLONE_SYSVSEM        = 0x40000,
    TUX_CLONE_SETTLS         = 0x80000,
    TUX_CLONE_PARENT_SETTID  = 0x100000,
    TUX_CLONE_CHILD_CLEARTID = 0x200000,
    TUX_CLONE_DETACHED       = 0x400000,
    TUX_CLONE_CHILD_SETTID   = 0x1000000,
};

#define TUX_CLONE_IO           0x80000000UL

enum {
    TUX_SIGCHLD = 0x11,
};

enum {
    TUX_PR_SET_NAME = 15,
};
