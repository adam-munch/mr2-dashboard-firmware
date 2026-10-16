/*
 * Minimal newlib system calls: stdout/stderr go to the debug UART, the heap
 * grows from the end of .bss up to the reserved stack area. Everything
 * else reports "not supported".
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#include "debug_uart.h"

/* Defined by the linker script. */
extern uint8_t end;
extern uint8_t _estack;
extern uint32_t _Min_Stack_Size;

int _write(int fd, const char *buf, int len);
int _read(int fd, char *buf, int len);
int _close(int fd);
int _lseek(int fd, int offset, int whence);
int _fstat(int fd, struct stat *st);
int _isatty(int fd);
int _getpid(void);
int _kill(int pid, int sig);
void _exit(int status);
void *_sbrk(ptrdiff_t incr);

int _write(int fd, const char *buf, int len)
{
    if (fd != 1 && fd != 2) {
        errno = EBADF;
        return -1;
    }
    debug_uart_write(buf, (size_t)len);
    return len;
}

int _read(int fd, char *buf, int len)
{
    (void)fd;
    (void)buf;
    (void)len;
    errno = ENOSYS;
    return -1;
}

int _close(int fd)
{
    (void)fd;
    return -1;
}

int _lseek(int fd, int offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;
    return 0;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

void _exit(int status)
{
    (void)status;
    for (;;) {
    }
}

void *_sbrk(ptrdiff_t incr)
{
    static uint8_t *heap_end;
    const uint8_t *limit = &_estack - (uint32_t)&_Min_Stack_Size;

    if (heap_end == NULL) {
        heap_end = &end;
    }
    if (heap_end + incr > limit) {
        errno = ENOMEM;
        return (void *)-1;
    }

    uint8_t *prev = heap_end;
    heap_end += incr;
    return prev;
}
