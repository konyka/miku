#include "miku_io.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>

typedef struct {
    miku_io_cb  cb;
    void       *data;
    int         events;
} io_watch_t;

struct miku_io_s {
    int           epfd;
    io_watch_t   *watches;
    int           max_fd;
    int           cap;
};

static int io_ensure_cap(miku_io_t *io, int fd) {
    if (fd < io->cap) return 0;
    int newcap = io->cap;
    while (newcap <= fd) newcap *= 2;
    io_watch_t *nw = (io_watch_t *)realloc(io->watches, newcap * sizeof(*nw));
    if (!nw) return -1;
    memset(nw + io->cap, 0, (newcap - io->cap) * sizeof(*nw));
    io->watches = nw;
    io->cap = newcap;
    return 0;
}

static int to_epoll_events(int events) {
    int ep = 0;
    if (events & MK_IO_READ)  ep |= EPOLLIN;
    if (events & MK_IO_WRITE) ep |= EPOLLOUT;
    if (events & MK_IO_ERROR) ep |= EPOLLERR;
    return ep;
}

static int from_epoll_events(int ep) {
    int ev = 0;
    if (ep & (EPOLLIN | EPOLLPRI))  ev |= MK_IO_READ;
    if (ep & EPOLLOUT)              ev |= MK_IO_WRITE;
    if (ep & EPOLLERR)              ev |= MK_IO_ERROR;
    if (ep & EPOLLHUP)              ev |= MK_IO_HUP;
    return ev;
}

miku_io_t *miku_io_create(void) {
    miku_io_t *io = (miku_io_t *)calloc(1, sizeof(*io));
    if (!io) return NULL;
    io->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (io->epfd < 0) { free(io); return NULL; }
    io->cap = 1024;
    io->watches = (io_watch_t *)calloc(io->cap, sizeof(*io->watches));
    if (!io->watches) { close(io->epfd); free(io); return NULL; }
    return io;
}

int miku_io_add(miku_io_t *io, int fd, int events, miku_io_cb cb, void *data) {
    if (!io || fd < 0) return -1;
    if (io_ensure_cap(io, fd) != 0) return -1;
    struct epoll_event ev;
    ev.events = to_epoll_events(events) | EPOLLET;
    ev.data.fd = fd;
    if (epoll_ctl(io->epfd, EPOLL_CTL_ADD, fd, &ev) != 0) return -1;
    io->watches[fd].cb = cb;
    io->watches[fd].data = data;
    io->watches[fd].events = events;
    if (fd >= io->max_fd) io->max_fd = fd + 1;
    return 0;
}

int miku_io_mod(miku_io_t *io, int fd, int events, miku_io_cb cb, void *data) {
    if (!io || fd < 0 || fd >= io->cap) return -1;
    struct epoll_event ev;
    ev.events = to_epoll_events(events) | EPOLLET;
    ev.data.fd = fd;
    if (epoll_ctl(io->epfd, EPOLL_CTL_MOD, fd, &ev) != 0) return -1;
    io->watches[fd].cb = cb;
    io->watches[fd].data = data;
    io->watches[fd].events = events;
    return 0;
}

int miku_io_del(miku_io_t *io, int fd) {
    if (!io || fd < 0) return -1;
    epoll_ctl(io->epfd, EPOLL_CTL_DEL, fd, NULL);
    if (fd < io->cap) {
        io->watches[fd].cb = NULL;
        io->watches[fd].data = NULL;
        io->watches[fd].events = 0;
    }
    return 0;
}

int miku_io_poll(miku_io_t *io, int timeout_ms) {
    if (!io) return -1;
    struct epoll_event events[128];
    int n = epoll_wait(io->epfd, events, 128, timeout_ms);
    if (n < 0) return -1;
    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
        if (fd < 0 || fd >= io->cap || !io->watches[fd].cb) continue;
        int revents = from_epoll_events(events[i].events);
        io->watches[fd].cb(fd, revents, io->watches[fd].data);
    }
    return n;
}

void miku_io_destroy(miku_io_t *io) {
    if (!io) return;
    close(io->epfd);
    free(io->watches);
    free(io);
}

int miku_io_fd(const miku_io_t *io) {
    return io ? io->epfd : -1;
}
