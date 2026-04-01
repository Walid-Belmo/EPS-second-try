/* =============================================================================
 * syscalls_min.c
 * Minimal C library system call stubs for bare-metal SAMD21.
 * Write once. Never touch again.
 *
 * Category: PURE LOGIC (no hardware)
 * =============================================================================
 *
 * The C library (newlib-nano) references a set of system calls that an
 * operating system would provide: _write, _read, _sbrk, etc. On bare metal
 * there is no OS, so we provide stubs that do nothing.
 *
 * This file is NOT included in the Makefile SRCS for Phase 0 because we use
 * --specs=nosys.specs instead. It exists here for when we need custom stubs
 * (e.g., redirecting _write for printf in future phases).
 */

#include <sys/stat.h>
#include <stdint.h>

/* Prototypes for newlib system call stubs. These have no standard header
 * because they are OS-provided functions that newlib expects to exist. */
void *_sbrk(int incr);
int   _write(int fd, char *ptr, int len);
int   _read(int fd, char *ptr, int len);
int   _close(int fd);
int   _fstat(int fd, struct stat *st);
int   _isatty(int fd);
int   _lseek(int fd, int ptr, int dir);

/* _sbrk: heap growth. We return -1 (error) because we never use malloc. */
void *_sbrk(int incr)
{
    (void)incr;
    return (void *)-1;
}

/* _write: used by printf. We do not use printf — return 0. */
int _write(int fd, char *ptr, int len)
{
    (void)fd;
    (void)ptr;
    (void)len;
    return 0;
}

int _read(int fd, char *ptr, int len)
{
    (void)fd;
    (void)ptr;
    (void)len;
    return 0;
}

int _close(int fd)
{
    (void)fd;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    (void)st;
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

int _lseek(int fd, int ptr, int dir)
{
    (void)fd;
    (void)ptr;
    (void)dir;
    return 0;
}
