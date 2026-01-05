#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <pthread.h>

#include "lfi_tux.h"

#include "config.h"
#include "lfi.h"
#include "types.h"
#include "futex.h"

enum {
    TUX_PATH_MAX   = 4096,
    TUX_NOFILE     = 128,
    TUX_BRKMAXSIZE = 512ULL * 1024 * 1024,
};

struct TuxProc;

struct FDFile {
    void* dev;
    size_t refs;
    pthread_mutex_t lk_refs;

    ssize_t (*read)(void*, uint8_t*, size_t);
    ssize_t (*write)(void*, uint8_t*, size_t);
    ssize_t (*lseek)(void*, off_t, int);
    int     (*close)(void*);
    int     (*stat_)(void*, struct Stat*);
    ssize_t (*getdents)(void*, void*, size_t);
    int     (*chown)(void*, tux_uid_t, tux_gid_t);
    int     (*chmod)(void*, tux_mode_t);
    int     (*truncate)(void*, off_t);
    int     (*sync)(void*);

    struct HostFile* (*file)(void*);
};

struct FDTable {
    struct FDFile* files[TUX_NOFILE];
    pthread_mutex_t lk;
};

struct Dir {
    struct HostFile* file;
    struct FDFile* fd;
    pthread_mutex_t lk;
};

#define LINUX_SIGHUP    1
#define LINUX_SIGINT    2
#define LINUX_SIGQUIT   3
#define LINUX_SIGILL    4
#define LINUX_SIGTRAP   5
#define LINUX_SIGABRT   6
#define LINUX_SIGBUS    7
#define LINUX_SIGFPE    8
#define LINUX_SIGKILL   9
#define LINUX_SIGUSR1   10
#define LINUX_SIGSEGV   11
#define LINUX_SIGUSR2   12
#define LINUX_SIGPIPE   13
#define LINUX_SIGALRM   14
#define LINUX_SIGTERM   15
#define LINUX_SIGSTKFLT 16
#define LINUX_SIGCHLD   17
#define LINUX_SIGCONT   18
#define LINUX_SIGSTOP   19
#define LINUX_SIGTSTP   20
#define LINUX_SIGTTIN   21
#define LINUX_SIGTTOU   22
#define LINUX_SIGURG    23
#define LINUX_SIGXCPU   24
#define LINUX_SIGXFSZ   25
#define LINUX_SIGVTALRM 26
#define LINUX_SIGPROF   27
#define LINUX_SIGWINCH  28
#define LINUX_SIGIO     29
#define LINUX_SIGPWR    30
#define LINUX_SIGSYS    31
#define LINUX_NSIG      32

#define LINUX_SIG_DFL   0
#define LINUX_SIG_IGN   1

struct SigActionEntry {
    bool valid;
    struct SigAction entry;
};

struct TuxProc {
    struct LFIAddrSpace* p_as;
    lfiptr_t brkbase;
    size_t brksize;
    struct LFIAddrSpace* p_jit_as;
    int jit_fd;
    uint8_t* jit_alias;
    pthread_mutex_t lk_as;
    pthread_mutex_t lk_brk;

    struct SigActionEntry signals[LINUX_NSIG];

    struct FDTable fdtable;
    struct Dir cwd;

#ifdef CONFIG_THREADS
    struct Futexes futexes;
#endif

    struct Tux* tux;
    struct LFIAddrSpaceInfo p_info;
    struct LFIAddrSpaceInfo p_jit_info;
    int pid;
};

struct TuxThread {
    struct LFIContext* p_ctx;
    lfiptr_t stack;

    uintptr_t ctid;
    int tid;

    struct TuxProc* proc;
};

int procmapat(struct TuxProc* p, lfiptr_t start, size_t size, int prot, int flags, int fd, off_t offset);

int procmapany(struct TuxProc* p, size_t size, int prot, int flags, int fd, off_t offset, lfiptr_t* o_mapstart);

int procunmap(struct TuxProc* p, lfiptr_t start, size_t size);

int procmapjitcode(struct TuxProc* p, size_t exec_size, size_t data_size, lfiptr_t* o_mapstart);

int procunmapjitcode(struct TuxProc* p, lfiptr_t start, size_t exec_size, size_t data_size);

int proccreatejitcode(struct TuxProc* p, lfiptr_t dst, uint8_t* src, size_t size);

int procmodifyjitcode(struct TuxProc* p, lfiptr_t dst, size_t value, size_t patch_len, int halt_pad);

int procdeletejitcode(struct TuxProc* p, lfiptr_t dst, size_t length);

struct TuxThread* procnewthread(struct TuxThread* p);
