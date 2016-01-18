/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "uv.h"
#include "internal.h"

#include <stddef.h> /* NULL */
#include <stdio.h> /* printf */
#include <stdlib.h>
#include <string.h> /* strerror */
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h> /* INT_MAX, PATH_MAX */
#include <sys/uio.h> /* writev */
#include <sys/resource.h> /* getrusage */

#ifdef __linux__
# include <sys/ioctl.h>
#endif

#ifdef __sun
# include <sys/types.h>
# include <sys/wait.h>
#endif

#ifdef __APPLE__
# include <mach-o/dyld.h> /* _NSGetExecutablePath */
# include <sys/filio.h>
# include <sys/ioctl.h>
#endif

#ifdef __FreeBSD__
# include <sys/sysctl.h>
# include <sys/filio.h>
# include <sys/ioctl.h>
# include <sys/wait.h>
# define UV__O_CLOEXEC O_CLOEXEC
# if __FreeBSD__ >= 10
#  define uv__accept4 accept4
#  define UV__SOCK_NONBLOCK SOCK_NONBLOCK
#  define UV__SOCK_CLOEXEC  SOCK_CLOEXEC
# endif
# if !defined(F_DUP2FD_CLOEXEC) && defined(_F_DUP2FD_CLOEXEC)
#  define F_DUP2FD_CLOEXEC  _F_DUP2FD_CLOEXEC
# endif
#endif

#ifdef _AIX
#include <sys/ioctl.h>
#endif

static void uv__run_pending(uv_loop_t* loop);

/* Verify that uv_buf_t is ABI-compatible with struct iovec. */
STATIC_ASSERT(sizeof(uv_buf_t) == sizeof(struct iovec));
STATIC_ASSERT(sizeof(&((uv_buf_t* ) 0)->base) == sizeof(((struct iovec* ) 0)->iov_base));
STATIC_ASSERT(sizeof(&((uv_buf_t* ) 0)->len) == sizeof(((struct iovec* ) 0)->iov_len));
STATIC_ASSERT(offsetof(uv_buf_t, base)== offsetof(struct iovec, iov_base));
STATIC_ASSERT(offsetof(uv_buf_t, len)== offsetof(struct iovec, iov_len));

uint64_t uv_hrtime(void)
{
    return uv__hrtime(UV_CLOCK_PRECISE);
}

unsigned int uv_close(uv_handle_t* handle)
{
    assert(!(handle->flags & UV_CLOSED));
    unsigned int reqcnt = 0;

    handle->flags |= UV_CLOSING;

    switch (handle->type)
    {
    case UV_NAMED_PIPE:
        uv__pipe_close((uv_pipe_t*) handle);
        break;

    case UV_TTY:
        reqcnt = uv__stream_close((uv_stream_t*) handle);
        break;

    case UV_TCP:
        reqcnt = uv__tcp_close((uv_tcp_t*) handle);
        break;

    case UV_UDP:
        reqcnt = uv__udp_close((uv_udp_t*) handle);
        break;

    case UV_PREPARE:
        uv__prepare_close((uv_prepare_t*) handle);
        break;

    case UV_CHECK:
        uv__check_close((uv_check_t*) handle);
        break;

    case UV_IDLE:
        uv__idle_close((uv_idle_t*) handle);
        break;

    case UV_ASYNC:
        uv__async_close((uv_async_t*) handle);
        break;

    case UV_TIMER:
        uv__timer_close((uv_timer_t*) handle);
        break;

    case UV_PROCESS:
        uv__process_close((uv_process_t*) handle);
        break;

    case UV_FS_EVENT:
        uv__fs_event_close((uv_fs_event_t*) handle);
        break;

    case UV_POLL:
        uv__poll_close((uv_poll_t*) handle);
        break;

    case UV_FS_POLL:
        uv__fs_poll_close((uv_fs_poll_t*) handle);
        break;

    case UV_SIGNAL:
        uv__signal_close((uv_signal_t*) handle);
        /* Signal handles may not be closed immediately. The signal code will */
        /* itself close uv__make_close_pending whenever appropriate. */
        break;

    default:
        assert(0);
    }

    if (reqcnt == 0)
    {
        handle->flags &= ~UV_CLOSING;
        handle->flags |= UV_CLOSED;
        uv__handle_close(handle);
    }
    return reqcnt;
}

int uv__socket_sockopt(uv_handle_t* handle, int optname, int* value)
{
    int r;
    int fd;
    socklen_t len;

    if (handle == null || value == null)
    {
        return -EINVAL;
    }

    if (handle->type == UV_TCP || handle->type == UV_NAMED_PIPE)
    {
        fd = uv__stream_fd((uv_stream_t* ) handle);
    }
    else if (handle->type == UV_UDP)
    {
        fd = ((uv_udp_t *) handle)->io_watcher.fd;
    }
    else
    {
        return -ENOTSUP;
    }

    len = sizeof(*value);

    if (*value == 0)
    {
        r = getsockopt(fd, SOL_SOCKET, optname, value, &len);
    }
    else
    {
        r = setsockopt(fd, SOL_SOCKET, optname, (const void*) value, len);
    }

    if (r < 0)
    {
        return -errno;
    }

    return 0;
}

int uv_is_closing(const uv_handle_t* handle)
{
    return uv__is_closing(handle);
}

int uv_backend_fd(const uv_loop_t* loop)
{
    return loop->backend_fd;
}

int uv_backend_timeout(const uv_loop_t* loop)
{
    if (loop->stop_flag != 0)
    {
        return 0;
    }

    if (!uv__has_active_handles(loop) && !uv__has_active_reqs(loop))
    {
        return 0;
    }

    if (!QUEUE_EMPTY(&loop->idle_handles))
    {
        return 0;
    }
#if 0
    if (loop->closing_handles)
    return 0;
#endif

    return uv__next_timeout(loop);
}

static int uv__loop_alive(const uv_loop_t* loop)
{
    return uv__has_active_handles(loop) || uv__has_active_reqs(loop);
}

int uv_loop_alive(const uv_loop_t* loop)
{
    return uv__loop_alive(loop);
}

#define UV__IO_RW_TIMES 64

static void uv__io_rable(uv_loop_t* loop)
{
    QUEUE* q;
    uv__io_t* w;
    uint32_t count = 0;

    while (!QUEUE_EMPTY(&loop->rable_queue) && count++ < UV__IO_RW_TIMES)
    {
//        printf("----> %s:%u.\n", __FUNCTION__, __LINE__);
        q = QUEUE_HEAD(&loop->rable_queue);
        QUEUE_REMOVE(q);
        QUEUE_INIT(q);
        w = QUEUE_DATA(q, uv__io_t, rable_queue);
        w->cb(loop, w, UV__EPOLLIN);
    }
}

static void uv__io_wable(uv_loop_t* loop)
{
    QUEUE* q;
    uv__io_t* w;
    uint32_t count = 0;

    while (!QUEUE_EMPTY(&loop->wable_queue) && count++ < (UV__IO_RW_TIMES * 2))
    {
        q = QUEUE_HEAD(&loop->wable_queue);
        QUEUE_REMOVE(q);
        QUEUE_INIT(q);
        w = QUEUE_DATA(q, uv__io_t, wable_queue);
        w->cb(loop, w, UV__EPOLLOUT);
    }
}

int uv_run(uv_loop_t* loop, uv_run_mode mode)
{
    int timeout = 0;
    int r = 0;

    DEBUG_PRINT();
    r = uv__loop_alive(loop);
    if (!r)
    {
        uv__update_time(loop);
    }

    while (r != 0 && loop->stop_flag == 0)
    {
        UV_TICK_START(loop, mode);

        uv__update_time(loop);
        uv__run_timers(loop);
        uv__run_pending(loop);
        uv__run_idle(loop);
        uv__run_prepare(loop);

        timeout = 0;
        if ((mode & UV_RUN_NOWAIT) == 0)
        {
            timeout = uv_backend_timeout(loop);
        }

        uv__io_poll(loop, timeout);
        uv__io_rable(loop);
        uv__io_wable(loop);
        uv__run_check(loop);
//        uv__run_closing_handles(loop);

        if (mode == UV_RUN_ONCE)
        {
            /* UV_RUN_ONCE implies forward progess: at least one callback must have
             * been invoked when it returns. uv__io_poll() can return without doing
             * I/O (meaning: no callbacks) when its timeout expires - which means we
             * have pending timers that satisfy the forward progress constraint.
             *
             * UV_RUN_NOWAIT makes no guarantees about progress so it's omitted from
             * the check.
             */
            uv__update_time(loop);
            uv__run_timers(loop);
        }

        DEBUG_PRINT();
        r = uv__loop_alive(loop);

        UV_TICK_STOP(loop, mode);

        DEBUG_PRINT();
        if (mode & (UV_RUN_ONCE | UV_RUN_NOWAIT))
        {
            break;
        }
    }

    /* The if statement lets gcc compile it to a conditional store. Avoids
     * dirtying a cache line.
     */
    if (loop->stop_flag != 0)
    {
        loop->stop_flag = 0;
    }

    return r;
}

void uv_update_time(uv_loop_t* loop)
{
    uv__update_time(loop);
}

int uv_is_active(const uv_handle_t* handle)
{
    return uv__is_active(handle);
}

/* Open a socket in non-blocking close-on-exec mode, atomically if possible. */
int uv__socket(int domain, int type, int protocol)
{
    int sockfd;
    int err;

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
    sockfd = socket(domain, type | SOCK_NONBLOCK | SOCK_CLOEXEC, protocol);
    if (sockfd != -1)
    {
        return sockfd;
    }

    if (errno != EINVAL)
    {
        return -errno;
    }
#endif

    sockfd = socket(domain, type, protocol);
    if (sockfd == -1)
    {
        return -errno;
    }

    err = uv__nonblock(sockfd, 1);
    if (err == 0)
    {
        err = uv__cloexec(sockfd, 1);
    }

    if (err)
    {
        uv__close(sockfd);
        return err;
    }

#if defined(SO_NOSIGPIPE)
    {
        int on = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
    }
#endif

    return sockfd;
}

int uv__accept(int sockfd)
{
    int peerfd;
    int err;

    assert(sockfd >= 0);

#if defined(__linux__) || __FreeBSD__ >= 10
    static int no_accept4;

    if (no_accept4)
    {
        goto skip;
    }

    peerfd = uv__accept4(sockfd, null, null, UV__SOCK_NONBLOCK | UV__SOCK_CLOEXEC);
    if (peerfd != -1)
    {
        return peerfd;
    }

    if (errno != ENOSYS)
    {
        return -errno;
    }

    no_accept4 = 1;
    skip:
#endif

    peerfd = accept(sockfd, null, null);
    if (peerfd == -1)
    {
        return -errno;
    }

    err = uv__cloexec(peerfd, 1);
    if (err == 0)
    {
        err = uv__nonblock(peerfd, 1);
    }

    if (err)
    {
        uv__close(peerfd);
        return err;
    }

    return peerfd;
}

int uv__close(int fd)
{
    int saved_errno;
    int rc;

    assert(fd > -1); /* Catch uninitialized io_watcher.fd bugs. */
    assert(fd > STDERR_FILENO); /* Catch stdio close bugs. */

    saved_errno = errno;
    rc = close(fd);
    if (rc == -1)
    {
        rc = -errno;
        if (rc == -EINTR)
        {
            rc = -EINPROGRESS; /* For platform/libc consistency. */
        }
        errno = saved_errno;
    }

    return rc;
}

int uv_eventfd(unsigned int count)
{
    int sfd;
    sfd = uv__eventfd2(count, UV__EFD_NONBLOCK | UV__EFD_CLOEXEC);
    if (sfd == -1 && (errno == ENOSYS || errno == EINVAL))
    {
        sfd = uv__eventfd(count);
        if (sfd != -1)
        {
            uv__nonblock(sfd, 1);
            uv__cloexec(sfd, 1);
        }
    }
    return sfd;
}

int uv_signalfd(int fd, const sigset_t* mask)
{
    int sfd;
    sfd = uv__signalfd4(fd, mask, UV__O_NONBLOCK | UV__O_CLOEXEC);
    if (sfd == -1 && (errno == ENOSYS || errno == EINVAL))
    {
        sfd = uv__signalfd(fd, mask);
        if (sfd != -1)
        {
            uv__nonblock(sfd, 1);
            uv__cloexec(sfd, 1);
        }
    }
    return sfd;
}

#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__) || defined(_AIX)

int uv__nonblock(int fd, int set)
{
    int r;

    do
    r = ioctl(fd, FIONBIO, &set);
    while (r == -1 && errno == EINTR);

    if (r)
    return -errno;

    return 0;
}

int uv__cloexec(int fd, int set)
{
    int r;

    do
    r = ioctl(fd, set ? FIOCLEX : FIONCLEX);
    while (r == -1 && errno == EINTR);

    if (r)
    return -errno;

    return 0;
}

#else /* !(defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)) */

int uv__nonblock(int fd, int set)
{
    int flags;
    int r;

    do
        r = fcntl(fd, F_GETFL);
    while (r == -1 && errno == EINTR);

    if (r == -1)
        return -errno;

    /* Bail out now if already set/clear. */
    if (!!(r & O_NONBLOCK) == !!set)
        return 0;

    if (set)
        flags = r | O_NONBLOCK;
    else
        flags = r & ~O_NONBLOCK;

    do
        r = fcntl(fd, F_SETFL, flags);
    while (r == -1 && errno == EINTR);

    if (r)
        return -errno;

    return 0;
}

int uv__cloexec(int fd, int set)
{
    int flags;
    int r;

    do
    {
        r = fcntl(fd, F_GETFD);
    } while (r == -1 && errno == EINTR);

    if (r == -1)
    {
        return -errno;
    }

    /* Bail out now if already set/clear. */
    if (!!(r & FD_CLOEXEC) == !!set)
    {
        return 0;
    }

    if (set)
    {
        flags = r | FD_CLOEXEC;
    }
    else
    {
        flags = r & ~FD_CLOEXEC;
    }

    do
    {
        r = fcntl(fd, F_SETFD, flags);
    } while (r == -1 && errno == EINTR);

    if (r)
    {
        return -errno;
    }

    return 0;
}

#endif /* defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__) */

/* This function is not execve-safe, there is a race window
 * between the call to dup() and fcntl(FD_CLOEXEC).
 */
int uv__dup(int fd)
{
    int err;

    fd = dup(fd);

    if (fd == -1)
        return -errno;

    err = uv__cloexec(fd, 1);
    if (err)
    {
        uv__close(fd);
        return err;
    }

    return fd;
}

ssize_t uv__recvmsg(int fd, struct msghdr* msg, int flags)
{
    struct cmsghdr* cmsg;
    ssize_t rc;
    int* pfd;
    int* end;
#if defined(__linux__)
    static int no_msg_cmsg_cloexec;
    if (no_msg_cmsg_cloexec == 0)
    {
        rc = recvmsg(fd, msg, flags | 0x40000000); /* MSG_CMSG_CLOEXEC */
        if (rc != -1)
        return rc;
        if (errno != EINVAL)
        return -errno;
        rc = recvmsg(fd, msg, flags);
        if (rc == -1)
        return -errno;
        no_msg_cmsg_cloexec = 1;
    }
    else
    {
        rc = recvmsg(fd, msg, flags);
    }
#else
    rc = recvmsg(fd, msg, flags);
#endif
    if (rc == -1)
        return -errno;
    if (msg->msg_controllen == 0)
        return rc;
    for (cmsg = CMSG_FIRSTHDR(msg); cmsg != null; cmsg = CMSG_NXTHDR(msg, cmsg))
        if (cmsg->cmsg_type == SCM_RIGHTS)
            for (pfd = (int*) CMSG_DATA(cmsg), end = (int*) ((char*) cmsg + cmsg->cmsg_len); pfd < end; pfd += 1)
                uv__cloexec(*pfd, 1);
    return rc;
}

int uv_cwd(char* buffer, size_t* size)
{
    if (buffer == null || size == null)
        return -EINVAL;

    if (getcwd(buffer, *size) == null)
        return -errno;

    *size = strlen(buffer) + 1;
    return 0;
}

int uv_chdir(const char* dir)
{
    if (chdir(dir))
        return -errno;

    return 0;
}

void uv_disable_stdio_inheritance(void)
{
    int fd;

    /* Set the CLOEXEC flag on all open descriptors. Unconditionally try the
     * first 16 file descriptors. After that, bail out after the first error.
     */
    for (fd = 0;; fd++)
        if (uv__cloexec(fd, 1) && fd > 15)
            break;
}

static void uv__run_pending(uv_loop_t* loop)
{
    QUEUE* q;
    uv__io_t* w;

    while (!QUEUE_EMPTY(&loop->pending_queue))
    {
        q = QUEUE_HEAD(&loop->pending_queue);
        QUEUE_REMOVE(q);
        QUEUE_INIT(q);

        w = QUEUE_DATA(q, uv__io_t, pending_queue);
        w->cb(loop, w, UV__POLLOUT);
    }
}

static unsigned int next_power_of_two(unsigned int val)
{
    val -= 1;
    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;
    val += 1;
    return val;
}

static void maybe_resize(uv_loop_t* loop, unsigned int len)
{
    uv__io_t** watchers;
    void* fake_watcher_list;
    void* fake_watcher_count;
    unsigned int nwatchers;
    unsigned int i;

    if (len <= loop->nwatchers)
    {
        return;
    }

    /* Preserve fake watcher list and count at the end of the watchers */
    if (loop->watchers != null)
    {
        fake_watcher_list = loop->watchers[loop->nwatchers];
        fake_watcher_count = loop->watchers[loop->nwatchers + 1];
    }
    else
    {
        fake_watcher_list = null;
        fake_watcher_count = null;
    }

    nwatchers = next_power_of_two(len + 2) - 2;
    watchers = realloc(loop->watchers, (nwatchers + 2) * sizeof(loop->watchers[0]));

    if (watchers == null)
    {
        abort();
    }
    for (i = loop->nwatchers; i < nwatchers; i++)
    {
        watchers[i] = null;
    }
    watchers[nwatchers] = fake_watcher_list;
    watchers[nwatchers + 1] = fake_watcher_count;

    loop->watchers = watchers;
    loop->nwatchers = nwatchers;
}

void uv__io_init(uv__io_t* w, uv__io_cb cb, int fd)
{
    assert(cb != null);
    assert(fd >= -1);
    QUEUE_INIT(&w->pending_queue);
    QUEUE_INIT(&w->watcher_queue);
    QUEUE_INIT(&w->rable_queue);
    QUEUE_INIT(&w->wable_queue);
    w->cb = cb;
    w->fd = fd;
    w->events = 0;
    w->pevents = 0;
    w->reqcnt = 0;
    w->step = 0;

#if defined(UV_HAVE_KQUEUE)
    w->rcount = 0;
    w->wcount = 0;
#endif /* defined(UV_HAVE_KQUEUE) */
}

//#define UV__IO_DEBUG(str...) fprintf(stderr, str)
#define UV__IO_DEBUG(str...)

unsigned int uv__io_start(uv_loop_t* loop, uv__io_t* w, unsigned int events)
{
//    int bywatch = 0;
    assert(0 == (events & ~(UV__POLLIN | UV__POLLOUT)));
    assert(0 != events);
    assert(w->fd >= 0);
    assert(w->fd < INT_MAX);

    w->pevents |= events;

    maybe_resize(loop, w->fd + 1);

    if (QUEUE_EMPTY(&w->watcher_queue))
    {
        QUEUE_INSERT_TAIL(&loop->watcher_queue, &w->watcher_queue);
        if (loop->watchers[w->fd] == NULL)
        {
            loop->watchers[w->fd] = w;
            loop->nfds++;
        }
        else
        {
            assert(loop->watchers[w->fd] == w);
        }
    }

    if ((w->pevents & UV__POLLIN) && (w->flags & UV_IO_RABLE))
    {
        if (QUEUE_EMPTY(&w->rable_queue))
        {
            QUEUE_INSERT_TAIL(&loop->rable_queue, &w->rable_queue);
        }
    }

    if ((w->pevents & UV__POLLOUT) && (w->flags & UV_IO_WABLE))
    {
        if (QUEUE_EMPTY(&w->wable_queue))
        {
            QUEUE_INSERT_TAIL(&loop->wable_queue, &w->wable_queue);
        }
    }

    return w->pevents;
}

unsigned int uv__io_stop(uv_loop_t* loop, uv__io_t* w, unsigned int events)
{
    assert(0 == (events & ~(UV__POLLIN | UV__POLLOUT)));
    assert(0 != events);

    if (w->fd == -1)
    {
        return 0;
    }

    assert(w->fd >= 0);

    /* Happens when uv__io_stop() is called on a handle that was never started. */
    if ((unsigned) w->fd >= loop->nwatchers)
    {
        return 0;
    }
    w->pevents &= ~events;

    if (w->pevents == 0)
    {
        QUEUE_REMOVE(&w->watcher_queue);
        QUEUE_INIT(&w->watcher_queue);

        QUEUE_REMOVE(&w->rable_queue);
        QUEUE_INIT(&w->rable_queue);

        QUEUE_REMOVE(&w->wable_queue);
        QUEUE_INIT(&w->wable_queue);

        if (loop->watchers[w->fd] != NULL)
        {
            assert(loop->watchers[w->fd] == w);
            assert(loop->nfds > 0);
            loop->watchers[w->fd] = NULL;
            loop->nfds--;
            w->events = 0;
        }
    }
    else
    {
        uv__io_start(loop, w, w->pevents);
    }
    return w->pevents;
}

void uv__io_close(uv_loop_t* loop, uv__io_t* w)
{
    uv__io_stop(loop, w, UV__POLLIN | UV__POLLOUT);
    QUEUE_REMOVE(&w->pending_queue);
    /* Remove stale events for this file descriptor */
    uv__platform_invalidate_fd(loop, w->fd);
}

void uv__io_feed(uv_loop_t* loop, uv__io_t* w)
{
    if (QUEUE_EMPTY(&w->pending_queue))
    {
        QUEUE_INSERT_TAIL(&loop->pending_queue, &w->pending_queue);
    }
}

int uv__io_active(const uv__io_t* w, unsigned int events)
{
    assert(0 == (events & ~(UV__POLLIN | UV__POLLOUT)));
    assert(0 != events);
    return (0 != (w->pevents & events));
}

int uv_getrusage(uv_rusage_t* rusage)
{
    struct rusage usage;

    if (getrusage(RUSAGE_SELF, &usage))
    {
        return -errno;
    }

    rusage->ru_utime.tv_sec = usage.ru_utime.tv_sec;
    rusage->ru_utime.tv_usec = usage.ru_utime.tv_usec;

    rusage->ru_stime.tv_sec = usage.ru_stime.tv_sec;
    rusage->ru_stime.tv_usec = usage.ru_stime.tv_usec;

    rusage->ru_maxrss = usage.ru_maxrss;
    rusage->ru_ixrss = usage.ru_ixrss;
    rusage->ru_idrss = usage.ru_idrss;
    rusage->ru_isrss = usage.ru_isrss;
    rusage->ru_minflt = usage.ru_minflt;
    rusage->ru_majflt = usage.ru_majflt;
    rusage->ru_nswap = usage.ru_nswap;
    rusage->ru_inblock = usage.ru_inblock;
    rusage->ru_oublock = usage.ru_oublock;
    rusage->ru_msgsnd = usage.ru_msgsnd;
    rusage->ru_msgrcv = usage.ru_msgrcv;
    rusage->ru_nsignals = usage.ru_nsignals;
    rusage->ru_nvcsw = usage.ru_nvcsw;
    rusage->ru_nivcsw = usage.ru_nivcsw;

    return 0;
}

int uv__open_cloexec(const char* path, int flags)
{
    int err;
    int fd;

#if defined(__linux__) || (defined(__FreeBSD__) && __FreeBSD__ >= 9)
    static int no_cloexec;

    if (!no_cloexec)
    {
        fd = open(path, flags | UV__O_CLOEXEC);
        if (fd != -1)
        {
            return fd;
        }

        if (errno != EINVAL)
        {
            return -errno;
        }

        /* O_CLOEXEC not supported. */
        no_cloexec = 1;
    }
#endif

    fd = open(path, flags);
    if (fd == -1)
    {
        return -errno;
    }

    err = uv__cloexec(fd, 1);
    if (err)
    {
        uv__close(fd);
        return err;
    }

    return fd;
}

int uv__dup2_cloexec(int oldfd, int newfd)
{
    int r;
#if defined(__FreeBSD__) && __FreeBSD__ >= 10
    do
    r = dup3(oldfd, newfd, O_CLOEXEC);
    while (r == -1 && errno == EINTR);
    if (r == -1)
    return -errno;
    return r;
#elif defined(__FreeBSD__) && defined(F_DUP2FD_CLOEXEC)
    do
    r = fcntl(oldfd, F_DUP2FD_CLOEXEC, newfd);
    while (r == -1 && errno == EINTR);
    if (r != -1)
    return r;
    if (errno != EINVAL)
    return -errno;
    /* Fall through. */
#elif defined(__linux__)
    static int no_dup3;
    if (!no_dup3)
    {
        do
        r = uv__dup3(oldfd, newfd, UV__O_CLOEXEC);
        while (r == -1 && (errno == EINTR || errno == EBUSY));
        if (r != -1)
        return r;
        if (errno != ENOSYS)
        return -errno;
        /* Fall through. */
        no_dup3 = 1;
    }
#endif
    {
        int err;
        do
            r = dup2(oldfd, newfd);
#if defined(__linux__)
        while (r == -1 && (errno == EINTR || errno == EBUSY));
#else
        while (r == -1 && errno == EINTR);
#endif

        if (r == -1)
            return -errno;

        err = uv__cloexec(newfd, 1);
        if (err)
        {
            uv__close(newfd);
            return err;
        }

        return r;
    }
}

int iov_update(struct iovec** iov, int iovcnt, int n, unsigned int* reqidx)
{
    struct iovec* _iov = (*iov);

    while (iovcnt > 0 && n >= _iov->iov_len)
    {
        n -= _iov->iov_len;
        _iov->iov_base = ((char*) _iov->iov_base) + _iov->iov_len;
        _iov->iov_len = 0;
        _iov++;
        iovcnt--;
        (*reqidx) = (*reqidx) + 1;
    }

    if (iovcnt && n > 0)
    {
        _iov->iov_base = ((char*) _iov->iov_base) + n;
        _iov->iov_len -= n;
    }
    *iov = _iov;
    return iovcnt;
}

ssize_t uvwrite(int fd, struct iovec* iov, unsigned int* reqidx)
{
    ssize_t n = 0;
    assert(iov && iov->iov_len > 0);

    do
    {
        n = write(fd, iov->iov_base, iov->iov_len);
        if (n > 0)
        {
            iov->iov_base = ((char*) iov->iov_base) + n;
            iov->iov_len -= n;
        }
    } while (((n >= 0) || (n < 0 && errno == EINTR)) && iov->iov_len > 0);

    if (iov->iov_len == 0)
    {
        *reqidx = (*reqidx) + 1;
    }

    return n;
}

ssize_t uvwritev(int fd, struct iovec* iov, int iovcnt, unsigned int* reqidx)
{
    ssize_t n = 0;

    assert(reqidx && iov && iovcnt > 0);

    do
    {
        n = writev(fd, iov, iovcnt);
        if (n > 0)
        {
            iovcnt = iov_update(&iov, iovcnt, n, reqidx);
        }
    } while (((n >= 0) || (n < 0 && errno == EINTR)) && iovcnt);

    return n;
}

ssize_t uvsendmsg(int sockfd, struct msghdr* msg, int flags, unsigned int* reqidx)
{
    ssize_t n = 0;
    assert(msg);
    struct iovec* iov = msg->msg_iov;
    int iovcnt = msg->msg_iovlen;
    do
    {
        n = sendmsg(sockfd, msg, flags);
        if (n > 0)
        {
            iovcnt = iov_update(&iov, iovcnt, n, reqidx);
            msg->msg_iov = iov;
            msg->msg_iovlen = iovcnt;
        }
    } while (((n >= 0) || (n < 0 && errno == EINTR)) && iovcnt);

    return n;
}

ssize_t uvread(int fd, uv_buf_t* rbuf, ssize_t* rlen)
{
    ssize_t n = 0;
    assert(rbuf && rlen);
    do
    {
        n = read(fd, rbuf->base + (*rlen), rbuf->len - (*rlen));
        if (n > 0)
        {
            (*rlen) = (*rlen) + n;
        }
    } while (((n > 0) || (n < 0 && errno == EINTR)) && rbuf->len > (*rlen));

    return n;
}

ssize_t uvrecvmsg(int sockfd, struct msghdr* msg, int flags, ssize_t* rlen)
{
#if 0
    struct cmsghdr* cmsg;
    ssize_t rc;
    int* pfd;
    int* end;
#if defined(__linux__)
    static int no_msg_cmsg_cloexec;
    if (no_msg_cmsg_cloexec == 0)
    {
        rc = recvmsg(fd, msg, flags | 0x40000000); /* MSG_CMSG_CLOEXEC */
        if (rc != -1)
        return rc;
        if (errno != EINVAL)
        return -errno;
        rc = recvmsg(fd, msg, flags);
        if (rc == -1)
        return -errno;
        no_msg_cmsg_cloexec = 1;
    }
    else
    {
        rc = recvmsg(fd, msg, flags);
    }
#else
    rc = recvmsg(sockfd, msg, flags);
#endif
    if (rc == -1)
    return -errno;
    if (msg->msg_controllen == 0)
    return rc;
    for (cmsg = CMSG_FIRSTHDR(msg); cmsg != null; cmsg = CMSG_NXTHDR(msg, cmsg))
    if (cmsg->cmsg_type == SCM_RIGHTS)
    for (pfd = (int*) CMSG_DATA(cmsg), end = (int*) ((char*) cmsg + cmsg->cmsg_len); pfd < end; pfd += 1)
    uv__cloexec(*pfd, 1);
    return rc;
#endif

    ssize_t n = 0;
    assert(msg);
    uv_buf_t rbuf;
    struct sockaddr_storage* addr = msg->msg_name;
    size_t namelen = msg->msg_namelen;

    *rlen = 0;
    do
    {
        msg->msg_name = addr;
        msg->msg_namelen = namelen;
        rbuf = (*((uv_buf_t*) msg->msg_iov));
        rbuf.base += (*rlen);
        if (*rlen <= rbuf.len)
        {
            rbuf.len -= (*rlen);
        }
        if (rbuf.len <= 0)
        {
            break;
        }
        msg->msg_iov = (struct iovec*) &rbuf;
        n = recvmsg(sockfd, msg, flags);
        if (n > 0)
        {
            (*rlen) = (*rlen) + n;
        }
    } while (((n > 0) || (n < 0 && errno == EINTR)) && rbuf.len > n);

    return n;
}

int uv_nonblock(int fd, int set)
{
    return uv__nonblock(fd, set);
}

