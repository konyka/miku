#ifndef MIKU_IO_H
#define MIKU_IO_H

#include "miku_common.h"

typedef struct miku_io_s miku_io_t;

#define MK_IO_READ    1
#define MK_IO_WRITE   2
#define MK_IO_ERROR   4
#define MK_IO_HUP     8

typedef void (*miku_io_cb)(int fd, int events, void *data);

MIKU_API miku_io_t *miku_io_create(void);
MIKU_API int  miku_io_add(miku_io_t *io, int fd, int events, miku_io_cb cb, void *data);
MIKU_API int  miku_io_mod(miku_io_t *io, int fd, int events, miku_io_cb cb, void *data);
MIKU_API int  miku_io_del(miku_io_t *io, int fd);
MIKU_API int  miku_io_poll(miku_io_t *io, int timeout_ms);
MIKU_API void miku_io_destroy(miku_io_t *io);
MIKU_API int  miku_io_fd(const miku_io_t *io);

static inline int miku_set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* Every socket write must go through this instead of write(2). A peer that has
 * gone away turns the second write into SIGPIPE, whose default action is to
 * terminate the process — so one client dropping at the wrong moment would take
 * the whole server down with every other session on it. MSG_NOSIGNAL suppresses
 * the signal per call and reports EPIPE instead, which the callers already
 * handle as a normal write failure. It is per-call rather than a process-wide
 * SIG_IGN so that linking this code cannot change a host program's signal
 * disposition; services additionally ignore SIGPIPE (miku_graceful_init) to
 * cover any path that still reaches write(2). */
static inline ssize_t miku_sock_write(int fd, const void *buf, size_t len) {
    return send(fd, buf, len, MSG_NOSIGNAL);
}

#endif
