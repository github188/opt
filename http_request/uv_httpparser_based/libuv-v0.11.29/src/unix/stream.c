/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <sys/sendfile.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>
#include <limits.h> /* IOV_MAX */

#define UV__CMSG_FD_COUNT 64
#define UV__CMSG_FD_SIZE (UV__CMSG_FD_COUNT * sizeof(int))

#if defined(__APPLE__)
# include <sys/event.h>
# include <sys/time.h>
# include <sys/select.h>

/* Forward declaration */
typedef struct uv__stream_select_s uv__stream_select_t;

struct uv__stream_select_s
{
    uv_stream_t* stream;
    uv_thread_t thread;
    uv_sem_t close_sem;
    uv_sem_t async_sem;
    uv_async_t async;
    int events;
    int fake_fd;
    int int_fd;
    int fd;
};
#endif /* defined(__APPLE__) */

static void uv__stream_osx_interrupt_select(uv_stream_t* stream)
{
#if defined(__APPLE__)
    /* Notify select() thread about state change */
    uv__stream_select_t* s;
    int r;

    s = stream->select;
    if (s == null)
    return;

    /* Interrupt select() loop
     * NOTE: fake_fd and int_fd are socketpair(), thus writing to one will
     * emit read event on other side
     */
    do
    r = write(s->fake_fd, "x", 1);
    while (r == -1 && errno == EINTR);

    assert(r == 1);
#else  /* !defined(__APPLE__) */
    /* No-op on any other platform */
#endif  /* !defined(__APPLE__) */
}

#if 0
#if defined(__APPLE__)
static void uv__stream_osx_select(void* arg)
{
    uv_stream_t* stream;
    uv__stream_select_t* s;
    char buf[1024];
    fd_set sread;
    fd_set swrite;
    int events;
    int fd;
    int r;
    int max_fd;

    stream = arg;
    s = stream->select;
    fd = s->fd;

    if (fd > s->int_fd)
    max_fd = fd;
    else
    max_fd = s->int_fd;

    while (1)
    {
        /* Terminate on semaphore */
        if (uv_sem_trywait(&s->close_sem) == 0)
        break;

        /* Watch fd using select(2) */
        FD_ZERO(&sread);
        FD_ZERO(&swrite);

        if (uv__io_active(&stream->io_watcher, UV__POLLIN))
        FD_SET(fd, &sread);
        if (uv__io_active(&stream->io_watcher, UV__POLLOUT))
        FD_SET(fd, &swrite);
        FD_SET(s->int_fd, &sread);

        /* Wait indefinitely for fd events */
        r = select(max_fd + 1, &sread, &swrite, null, null);
        if (r == -1)
        {
            if (errno == EINTR)
            continue;

            /* XXX: Possible?! */
            abort();
        }

        /* Ignore timeouts */
        if (r == 0)
        continue;

        /* Empty socketpair's buffer in case of interruption */
        if (FD_ISSET(s->int_fd, &sread))
        while (1)
        {
            r = read(s->int_fd, buf, sizeof(buf));

            if (r == sizeof(buf))
            continue;

            if (r != -1)
            break;

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;

            if (errno == EINTR)
            continue;

            abort();
        }

        /* Handle events */
        events = 0;
        if (FD_ISSET(fd, &sread))
        events |= UV__POLLIN;
        if (FD_ISSET(fd, &swrite))
        events |= UV__POLLOUT;

        assert(events != 0 || FD_ISSET(s->int_fd, &sread));
        if (events != 0)
        {
            ACCESS_ONCE(int, s->events) = events;

            uv_async_send(&s->async);
            uv_sem_wait(&s->async_sem);

            /* Should be processed at this stage */
            assert((s->events == 0) || (stream->flags & UV_CLOSING));
        }
    }
}

static void uv__stream_osx_select_cb(uv_async_t* handle)
{
    uv__stream_select_t* s;
    uv_stream_t* stream;
    int events;

    s = container_of(handle, uv__stream_select_t, async);
    stream = s->stream;

    /* Get and reset stream's events */
    events = s->events;
    ACCESS_ONCE(int, s->events) = 0;
    uv_sem_post(&s->async_sem);

    assert(events != 0);
    assert(events == (events & (UV__POLLIN | UV__POLLOUT)));

    /* Invoke callback on event-loop */
    if ((events & UV__POLLIN) && uv__io_active(&stream->io_watcher, UV__POLLIN))
    uv__stream_io(stream->loop, &stream->io_watcher, UV__POLLIN);

    if ((events & UV__POLLOUT) && uv__io_active(&stream->io_watcher, UV__POLLOUT))
    uv__stream_io(stream->loop, &stream->io_watcher, UV__POLLOUT);
}

static void uv__stream_osx_cb_close(uv_handle_t* async)
{
    uv__stream_select_t* s;

    s = container_of(async, uv__stream_select_t, async);
    free(s);
}

int uv__stream_try_select(uv_stream_t* stream, int* fd)
{
    /*
     * kqueue doesn't work with some files from /dev mount on osx.
     * select(2) in separate thread for those fds
     */

    struct kevent filter[1];
    struct kevent events[1];
    struct timespec timeout;
    uv__stream_select_t* s;
    int fds[2];
    int err;
    int ret;
    int kq;
    int old_fd;

    kq = kqueue();
    if (kq == -1)
    {
        perror("(libuv) kqueue()");
        return -errno;
    }

    EV_SET(&filter[0], *fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, 0);

    /* Use small timeout, because we only want to capture EINVALs */
    timeout.tv_sec = 0;
    timeout.tv_nsec = 1;

    ret = kevent(kq, filter, 1, events, 1, &timeout);
    uv__close(kq);

    if (ret == -1)
    return -errno;

    if (ret == 0 || (events[0].flags & EV_ERROR) == 0 || events[0].data != EINVAL)
    return 0;

    /* At this point we definitely know that this fd won't work with kqueue */
    s = malloc(sizeof(*s));
    if (s == null)
    return -ENOMEM;

    s->events = 0;
    s->fd = *fd;

    err = uv_async_init(stream->loop, &s->async, uv__stream_osx_select_cb);
    if (err)
    {
        free(s);
        return err;
    }

    s->async.flags |= UV__HANDLE_INTERNAL;
    uv__handle_unref(&s->async);

    if (uv_sem_init(&s->close_sem, 0))
    goto fatal1;

    if (uv_sem_init(&s->async_sem, 0))
    goto fatal2;

    /* Create fds for io watcher and to interrupt the select() loop. */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds))
    goto fatal3;

    s->fake_fd = fds[0];
    s->int_fd = fds[1];

    old_fd = *fd;
    s->stream = stream;
    stream->select = s;
    *fd = s->fake_fd;

    if (uv_thread_create(&s->thread, uv__stream_osx_select, stream))
    goto fatal4;

    return 0;

    fatal4:
    s->stream = null;
    stream->select = null;
    *fd = old_fd;
    uv__close(s->fake_fd);
    uv__close(s->int_fd);
    s->fake_fd = -1;
    s->int_fd = -1;
    fatal3:
    uv_sem_destroy(&s->async_sem);
    fatal2:
    uv_sem_destroy(&s->close_sem);
    fatal1:
    uv_close((uv_handle_t*) &s->async, uv__stream_osx_cb_close);
    return -errno;
}
#endif /* defined(__APPLE__) */
#endif

int uv__stream_open(uv_stream_t* stream, int fd, int flags)
{
    assert(fd >= 0);
    stream->flags |= flags;

    if (stream->type == UV_TCP)
    {
        if (uv__nonblock(fd, 1))
        {
            return -errno;
        }
        if ((stream->flags & UV_TCP_NODELAY) && uv__tcp_nodelay(fd, 1))
        {
            return -errno;
        }

        /* TODO Use delay the user passed in. */
        if ((stream->flags & UV_TCP_KEEPALIVE) && uv__tcp_keepalive(fd, 1, 60))
        {
            return -errno;
        }
    }

    stream->io_watcher.fd = fd;

    return 0;
}

#if 0
/* Implements a best effort approach to mitigating accept() EMFILE errors.
 * We have a spare file descriptor stashed away that we close to get below
 * the EMFILE limit. Next, we accept all pending connections and close them
 * immediately to signal the clients that we're overloaded - and we are, but
 * we still keep on trucking.
 *
 * There is one caveat: it's not reliable in a multi-threaded environment.
 * The file descriptor limit is per process. Our party trick fails if another
 * thread opens a file or creates a socket in the time window between us
 * calling close() and accept().
 */
static int uv__emfile_trick(uv_loop_t* loop, int accept_fd)
{
    int err;
    int emfile_fd;

    if (loop->emfile_fd == -1)
    {
        return -EMFILE;
    }

    uv__close(loop->emfile_fd);
    loop->emfile_fd = -1;

    do
    {
        err = uv__accept(accept_fd);
        if (err >= 0)
        {
            uv__close(err);
        }
    }while (err >= 0 || err == -EINTR);

    emfile_fd = uv__open_cloexec("/", O_RDONLY);
    if (emfile_fd >= 0)
    loop->emfile_fd = emfile_fd;

    return err;
}
#endif

#if defined(UV_HAVE_KQUEUE)
# define UV_DEC_BACKLOG(w) w->rcount--;
#else
# define UV_DEC_BACKLOG(w) /* no-op */
#endif /* defined(UV_HAVE_KQUEUE) */

void uv__server_io(uv_loop_t* loop, uv__io_t* w, unsigned int events)
{
    uv_stream_t* stream = null;
    int err = 0;

    stream = container_of(w, uv_stream_t, io_watcher);
    assert(events == UV__POLLIN);
    assert(stream->accepted_fd == -1);
    assert(!(stream->flags & UV_CLOSING));

    err = uv__accept(uv__stream_fd(stream));
    if (err < 0)
    {
        if (err == -EAGAIN || err == -EWOULDBLOCK || err == -ECONNABORTED || err == -EPROTO || err == -EINTR)
        {
            DEBUG_PRINT();
            uv__io_start(stream->loop, &stream->io_watcher, UV__POLLIN);
            return; /* Not an error. */
        }
    }
    else
    {
        DEBUG_PRINT();
        uv__io_start(stream->loop, &stream->io_watcher, UV__POLLIN);
    }

    UV_DEC_BACKLOG(w)
    stream->accepted_fd = err;
    stream->connection_cb(stream, err < 0 ? err : 0, stream->data);
    assert(stream->accepted_fd == -1);
}

#undef UV_DEC_BACKLOG

int uv_accept(uv_stream_t* server, uv_stream_t* client)
{
    int err = 0;

    /* TODO document this */
    assert(server->loop == client->loop);

    if (server->accepted_fd == -1)
    {
        return -EAGAIN;
    }

    switch (client->type)
    {
    case UV_NAMED_PIPE:
    case UV_TCP:
        err = uv__stream_open(client, server->accepted_fd, UV_STREAM_READABLE | UV_STREAM_WRITABLE);
        if (err)
        {
            /* TODO handle error */
            uv__close(server->accepted_fd);
            goto done;
        }
        break;

    case UV_UDP:
        err = uv_udp_open((uv_udp_t*) client, server->accepted_fd);
        if (err)
        {
            uv__close(server->accepted_fd);
            goto done;
        }
        break;

    default:
        return -EINVAL;
    }

//    client->io_watcher.flags |= UV_IO_RABLE;
//    client->io_watcher.flags |= UV_IO_WABLE;

    done:
    /* Process queued fds */
    if (server->queued_fds != null)
    {
        uv__stream_queued_fds_t* queued_fds;

        queued_fds = server->queued_fds;

        /* Read first */
        server->accepted_fd = queued_fds->fds[0];

        /* All read, free */
        assert(queued_fds->offset > 0);
        if (--queued_fds->offset == 0)
        {
            free(queued_fds);
            server->queued_fds = null;
        }
        else
        {
            /* Shift rest */
            memmove(queued_fds->fds, queued_fds->fds + 1, queued_fds->offset * sizeof(*queued_fds->fds));
        }
    }
    else
    {
        server->accepted_fd = -1;
        if (err == 0)
        {
            DEBUG_PRINT();
            uv__io_start(server->loop, &server->io_watcher, UV__POLLIN);
        }
    }
    return err;
}

int uv_listen(uv_stream_t* stream, int backlog, uv_connection_cb cb, void* user_data)
{
    int err;

    stream->data = user_data;

    switch (stream->type)
    {
    case UV_TCP:
        err = uv_tcp_listen((uv_tcp_t*) stream, backlog, cb);
        break;

    case UV_NAMED_PIPE:
        err = uv_pipe_listen((uv_pipe_t*) stream, backlog, cb);
        break;

    default:
        err = -EINVAL;
    }

    if (err == 0)
    {
        uv__handle_start(stream);
    }

    return err;
}

#if 0
static void uv__drain(uv_stream_t* stream)
{
    uv_shutdown_t* req;
    int err;

    assert(QUEUE_EMPTY(&stream->write_queue));
    uv__io_stop(stream->loop, &stream->io_watcher, UV__POLLOUT);
    uv__stream_osx_interrupt_select(stream);

    /* Shutdown? */
    if ((stream->flags & UV_STREAM_SHUTTING) && !(stream->flags & UV_CLOSING) && !(stream->flags & UV_STREAM_SHUT))
    {
        assert(stream->shutdown_req);

        req = stream->shutdown_req;
        stream->shutdown_req = null;
        stream->flags &= ~UV_STREAM_SHUTTING;
        uv__req_unregister(stream->loop, req);

        err = 0;
        if (shutdown(uv__stream_fd(stream), SHUT_WR))
        {
            err = -errno;
        }

        if (err == 0)
        {
            stream->flags |= UV_STREAM_SHUT;
        }

        if (req->cb != null)
        {
            req->cb(req, err);
        }
    }
}
#endif

static size_t uv__write_req_size(const uv_buf_t bufs[], unsigned int nbufs)
{
    size_t size;

    assert(bufs != null);
    size = uv__count_bufs(bufs, nbufs);

    return size;
}

static void uv__sendfile_complete(uv_write_base_t* base, int error)
{
    uv_stream_t* stream = base->stream;
    uv_sendfile_t* req = container_of(base, uv_sendfile_t, base);
    req->error = error;
    stream->io_watcher.reqcnt--;

    uv__req_unregister(stream->loop, req);

    if (req->sendfile_cb)
    {
        req->sendfile_cb(stream, req->in_fd, req->offset + req->size_writed_all, req->size_writed_all, error, req->data);
    }

    free(req);
}

static void uv__stream_write_complete(uv_write_base_t* base, int error)
{
    uv_stream_t* stream = base->stream;
    uv_write_t* req = container_of(base, uv_write_t, base);
    req->error = error;

    uv__req_unregister(stream->loop, req);
    stream->io_watcher.reqcnt--;
    stream->write_queue_size -= uv__write_req_size(req->bufs_backup, req->nbufs);

    if (req->write_cb)
    {
        req->write_cb(stream, req->error, req->bufs_backup, req->nbufs, req->data);
    }

    if (req->bufs != req->bufsml)
    {
        free(req->bufs);
        free(req->bufs_backup);
        req->bufs = null;
        req->bufs_backup = null;
    }
    free(req);
}

static int uv__handle_fd(uv_handle_t* handle)
{
    switch (handle->type)
    {
    case UV_NAMED_PIPE:
    case UV_TCP:
        return ((uv_stream_t*) handle)->io_watcher.fd;

    case UV_UDP:
        return ((uv_udp_t*) handle)->io_watcher.fd;

    default:
        return -1;
    }
}

static int uv__getiovmax()
{
#if defined(IOV_MAX)
    return IOV_MAX;
#elif defined(_SC_IOV_MAX)
    static int iovmax = -1;
    if (iovmax == -1)
    iovmax = sysconf(_SC_IOV_MAX);
    return iovmax;
#else
    return 1024;
#endif
}

static int uv__do_write(uv_stream_t* stream, uv_write_t* req)
{
    struct iovec* iov = null;
    int iovmax = 0;
    int reqcnt = 0;
    int iovcnt = 0;
    ssize_t n = 0;

    do
    {
        iov = null;
        iovmax = 0;
        reqcnt = 0;
        iovcnt = 0;
        n = 0;

        /*
         * Cast to iovec. We had to have our own uv_buf_t instead of iovec
         * because Windows's WSABUF is not an iovec.
         */
        assert(sizeof(uv_buf_t) == sizeof(struct iovec));
        iov = (struct iovec*) &(req->bufs[req->write_index]);
        reqcnt = req->nbufs - req->write_index;

        iovmax = uv__getiovmax();

        /* Limit iov count to avoid EINVALs from writev() */
        if (reqcnt > iovmax)
        {
            iovcnt = iovmax;
        }
        else
        {
            iovcnt = reqcnt;
        }

        assert(iovcnt > 0);

        /*
         * Now do the actual writev. Note that we've been updating the pointers
         * inside the iov each time we write. So there is no need to offset it.
         */

        if (req->send_handle)
        {
            struct msghdr msg;
            char scratch[64];
            struct cmsghdr *cmsg;
            int fd_to_send = uv__handle_fd((uv_handle_t*) req->send_handle);

            assert(fd_to_send >= 0);

            msg.msg_name = null;
            msg.msg_namelen = 0;
            msg.msg_iov = iov;
            msg.msg_iovlen = iovcnt;
            msg.msg_flags = 0;

            msg.msg_control = (void*) scratch;
            msg.msg_controllen = CMSG_SPACE(sizeof(fd_to_send));

            cmsg = CMSG_FIRSTHDR(&msg);
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type = SCM_RIGHTS;
            cmsg->cmsg_len = CMSG_LEN(sizeof(fd_to_send));

            /* silence aliasing warning */
            {
                void* pv = CMSG_DATA(cmsg);
                int* pi = pv;
                *pi = fd_to_send;
            }

            n = uvsendmsg(uv__stream_fd(stream), &msg, 0, &req->write_index);
        }
        else
        {
            if (iovcnt == 1)
            {
                n = uvwrite(uv__stream_fd(stream), iov, &req->write_index);
            }
            else
            {
                n = uvwritev(uv__stream_fd(stream), iov, iovcnt, &req->write_index);
            }
        }

        if (n < 0)
        {
            //must error
            assert(errno);
            return -errno;
        }

    } while (req->write_index < req->nbufs);

    // if req->bufs[req->nbufs - 1].len != 0, n should be < 0
    assert(req->bufs[req->nbufs - 1].len == 0);

    return UV_SUCCESS;
}

static void uv__sendfile(uv_write_base_t* base, QUEUE* h)
{
    uv_stream_t* stream = base->stream;
    uv_sendfile_t* req = container_of(base, uv_sendfile_t, base);
    QUEUE* q = &base->queue;

    ssize_t size = UV_SUCCESS;
    off_t offset = 0;

    assert(uv__stream_fd(stream) >= 0);
    assert(!(stream->flags & UV_STREAM_BLOCKING));
    assert(req->size_need_write > req->size_writed_all);

    offset = req->offset + req->size_writed_all;
    size = sendfile(uv__stream_fd(stream), req->in_fd, &offset, req->size_need_write - req->size_writed_all);
    if (size > 0)
    {
        stream->io_watcher.flags |= UV_IO_WABLE;
        req->size_writed_all += size;
    }
    else
    {
        stream->io_watcher.flags &= (~UV_IO_WABLE);
    }

    if ((size < 0 && errno != EAGAIN) || req->size_need_write == req->size_writed_all)
    {
        QUEUE_REMOVE(q);
        QUEUE_INIT(q);
        if (QUEUE_EMPTY(h))
        {
            if (!uv__io_stop(stream->loop, &stream->io_watcher, UV__POLLOUT))
            {
                uv__handle_stop(stream);
            }
        }
        else
        {
            if (uv__io_start(stream->loop, &stream->io_watcher, UV__POLLOUT))
            {
                uv__handle_start(stream);
            }
        }
        uv__sendfile_complete(base, size < 0 ? -errno : 0);
    }
    else
    {
        assert(req->size_need_write > req->size_writed_all);
        if (uv__io_start(stream->loop, &stream->io_watcher, UV__POLLOUT))
        {
            uv__handle_start(stream);
        }
    }
}

static void uv__stream_write(uv_write_base_t* base, QUEUE* h)
{
    int err = UV_SUCCESS;
    uv_stream_t* stream = base->stream;
    uv_write_t* req = container_of(base, uv_write_t, base);

    err = uv__do_write(stream, req);
    if (err && (err == -EAGAIN || err == -EWOULDBLOCK || err == -EINTR))
    {
        stream->io_watcher.flags &= (~UV_IO_WABLE);
        if (uv__io_start(stream->loop, &stream->io_watcher, UV__POLLOUT))
        {
            uv__handle_start(stream);
        }
    }
    else
    {
        stream->io_watcher.flags |= UV_IO_WABLE;
        QUEUE_REMOVE(&base->queue);
        QUEUE_INIT(&base->queue);
        if (QUEUE_EMPTY(h))
        {
            if (!uv__io_stop(stream->loop, &stream->io_watcher, UV__POLLOUT))
            {
                uv__handle_stop(stream);
            }
        }
        else
        {
            if (uv__io_start(stream->loop, &stream->io_watcher, UV__POLLOUT))
            {
                uv__handle_start(stream);
            }
        }
        uv__stream_write_complete(base, err);
    }
}

uv_handle_type uv__handle_type(int fd)
{
    struct sockaddr_storage ss;
    socklen_t len;
    int type;

    memset(&ss, 0, sizeof(ss));
    len = sizeof(ss);

    if (getsockname(fd, (struct sockaddr*) &ss, &len))
        return UV_UNKNOWN_HANDLE;

    len = sizeof type;

    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len))
        return UV_UNKNOWN_HANDLE;

    if (type == SOCK_STREAM)
    {
        switch (ss.ss_family)
        {
        case AF_UNIX:
            return UV_NAMED_PIPE;
        case AF_INET:
        case AF_INET6:
            return UV_TCP;
        }
    }

    if (type == SOCK_DGRAM && (ss.ss_family == AF_INET || ss.ss_family == AF_INET6))
        return UV_UDP;

    return UV_UNKNOWN_HANDLE;
}

#if 0
static void uv__stream_eof(uv_stream_t* stream)
{
    stream->flags |= UV_STREAM_READ_EOF;
    if (!uv__io_active(&stream->io_watcher, UV__POLLOUT))
    {
        uv__handle_stop(stream);
    }
    uv__stream_osx_interrupt_select(stream);
    stream->read_cb(stream, UV_EOF, &stream->rbuf, stream->data);
}

static int uv__stream_queue_fd(uv_stream_t* stream, int fd)
{
    uv__stream_queued_fds_t* queued_fds;
    unsigned int queue_size;

    queued_fds = stream->queued_fds;
    if (queued_fds == null)
    {
        queue_size = 8;
        queued_fds = malloc((queue_size - 1) * sizeof(*queued_fds->fds) + sizeof(*queued_fds));
        if (queued_fds == null)
        return -ENOMEM;
        queued_fds->size = queue_size;
        queued_fds->offset = 0;
        stream->queued_fds = queued_fds;

        /* Grow */
    }
    else if (queued_fds->size == queued_fds->offset)
    {
        queue_size = queued_fds->size + 8;
        queued_fds = realloc(queued_fds, (queue_size - 1) * sizeof(*queued_fds->fds) + sizeof(*queued_fds));

        /*
         * Allocation failure, report back.
         * NOTE: if it is fatal - sockets will be closed in uv__stream_close
         */
        if (queued_fds == null)
        return -ENOMEM;
        queued_fds->size = queue_size;
        stream->queued_fds = queued_fds;
    }

    /* Put fd in a queue */
    queued_fds->fds[queued_fds->offset++] = fd;

    return 0;
}

static int uv__stream_recv_cmsg(uv_stream_t* stream, struct msghdr* msg)
{
    struct cmsghdr* cmsg;

    for (cmsg = CMSG_FIRSTHDR(msg); cmsg != null; cmsg = CMSG_NXTHDR(msg, cmsg))
    {
        char* start;
        char* end;
        int err;
        void* pv;
        int* pi;
        unsigned int i;
        unsigned int count;

        if (cmsg->cmsg_type != SCM_RIGHTS)
        {
            fprintf(stderr, "ignoring non-SCM_RIGHTS ancillary data: %d\n", cmsg->cmsg_type);
            continue;
        }

        /* silence aliasing warning */
        pv = CMSG_DATA(cmsg);
        pi = pv;

        /* Count available fds */
        start = (char*) cmsg;
        end = (char*) cmsg + cmsg->cmsg_len;
        count = 0;
        while (start + CMSG_LEN(count * sizeof(*pi)) < end)
        {
            count++;
        }
        assert(start + CMSG_LEN(count * sizeof(*pi)) == end);

        for (i = 0; i < count; i++)
        {
            /* Already has accepted fd, queue now */
            if (stream->accepted_fd != -1)
            {
                err = uv__stream_queue_fd(stream, pi[i]);
                if (err != 0)
                {
                    /* Close rest */
                    for (; i < count; i++)
                    {
                        uv__close(pi[i]);
                    }
                    return err;
                }
            }
            else
            {
                stream->accepted_fd = pi[i];
            }
        }
    }

    return 0;
}
#endif

int uv_shutdown(uv_shutdown_t* req, uv_stream_t* stream, uv_shutdown_cb cb)
{
    assert((stream->type == UV_TCP || stream->type == UV_NAMED_PIPE) && "uv_shutdown (unix) only supports uv_handle_t right now");

    if (!(stream->flags & UV_STREAM_WRITABLE) || (stream->flags & UV_STREAM_SHUT) || (stream->flags & UV_STREAM_SHUTTING)
        || (stream->flags & UV_CLOSED) || (stream->flags & UV_CLOSING))
    {
        return -ENOTCONN;
    }

    assert(uv__stream_fd(stream) >= 0);

    /* Initialize request */
    uv__req_init(stream->loop, req, UV_SHUTDOWN);
    req->handle = stream;
    req->cb = cb;
    stream->shutdown_req = req;
    stream->flags |= UV_STREAM_SHUTTING;

    DEBUG_PRINT();
    uv__io_start(stream->loop, &stream->io_watcher, UV__POLLOUT);
    uv__stream_osx_interrupt_select(stream);

    return 0;
}

static void uv__stream_write_error(uv_stream_t* stream, int error)
{
    QUEUE* q = null;
    uv_write_base_t* base = null;

    q = QUEUE_HEAD(&stream->write_queue);
    QUEUE_REMOVE(q);
    QUEUE_INIT(q);

    base = QUEUE_DATA(q, uv_write_base_t, queue);
    base->cancel_cb(base, error);
}

static void uv__stream_connect_complete(uv_stream_t* stream, int error)
{
    QUEUE* h = (QUEUE*) &stream->write_queue;
    uv_connect_t* req = stream->connect_req;
    stream->connect_req = null;
    stream->io_watcher.reqcnt--;

    uv__req_unregister(stream->loop, req);
    if (QUEUE_EMPTY(h))
    {
        if (!uv__io_stop(stream->loop, &stream->io_watcher, UV__POLLOUT))
        {
            uv__handle_stop(stream);
        }
    }

//    assert(QUEUE_EMPTY(&stream->loop->watcher_queue));

    req->cb((uv_tcp_t*) req->handle, error, req->data);
    free(req);
}

static void uv__stream_read_complete(uv_stream_t* stream, int len)
{
    uv_read_cb read_cb = stream->read_cb;
    stream->read_cb = null;
    stream->io_watcher.reqcnt--;
    read_cb(stream, len, &stream->rbuf, stream->data);
}

static void uv__stream_error(uv_stream_t* stream, int error)
{
    /* call one of callbacks when error */
    int cancel_c = stream->connect_req ? 1 : 0;
    int cancel_r = stream->read_cb ? 1 : 0;
    int cancel_w = (!QUEUE_EMPTY(&stream->write_queue)) ? 1 : 0;

    assert(error);
    assert(stream->io_watcher.reqcnt);

    uv__handle_stop(stream);

    do
    {
        if (cancel_c)
        {
            uv__stream_connect_complete(stream, error);
            break;
        }
        if (cancel_r)
        {
            uv__stream_read_complete(stream, error);
            break;
        }
        if (cancel_w)
        {
            uv__stream_write_error(stream, error);
            break;
        }

    } while (0);
}

/**
 * We get called here from directly following a call to connect(2).
 * In order to determine if we've errored out or succeeded must call
 * getsockopt.
 */
static void uv__stream_connect(uv_stream_t* stream)
{
    int error = 0;
    socklen_t errorsize = sizeof(int);

    assert(stream->type == UV_TCP || stream->type == UV_NAMED_PIPE);
    assert(stream->connect_req);

    if (stream->delayed_error)
    {
        /* To smooth over the differences between unixes errors that
         * were reported synchronously on the first connect can be delayed
         * until the next tick--which is now.
         */
        error = stream->delayed_error;
        stream->delayed_error = 0;
    }
    else
    {
        /* Normal situation: we need to get the socket error from the kernel. */
        assert(uv__stream_fd(stream) >= 0);
        getsockopt(uv__stream_fd(stream), SOL_SOCKET, SO_ERROR, &error, &errorsize);
        error = -error;
    }

    if (error == -EINPROGRESS)
    {
        return;
    }

    uv__stream_connect_complete(stream, error);
}

static void uv__stream_read(uv_stream_t* stream)
{
    ssize_t n = 0; /* last read result */
    int isipc = 0;

    isipc = stream->type == UV_NAMED_PIPE && ((uv_pipe_t*) stream)->ipc;

    assert(stream->rbuf.len != 0);

    if (!uv__io_stop(stream->loop, &stream->io_watcher, UV__POLLIN))
    {
        uv__handle_stop(stream);
    }

    if (isipc)
    {
        struct msghdr msg;
        char cmsg_space[CMSG_SPACE(UV__CMSG_FD_SIZE)];
        /* ipc uses recvmsg */
        msg.msg_flags = 0;
        msg.msg_iov = (struct iovec*) &stream->rbuf;
        msg.msg_iovlen = 1;
        msg.msg_name = null;
        msg.msg_namelen = 0;
        /* Set up to receive a descriptor even if one isn't in the message */
        msg.msg_controllen = sizeof(cmsg_space);
        msg.msg_control = cmsg_space;

        n = uvrecvmsg(uv__stream_fd(stream), &msg, 0, &stream->rlen);
    }
    else
    {
        n = uvread(uv__stream_fd(stream), &stream->rbuf, &stream->rlen);
    }

    if (n < 0)
    {
        assert(errno);
        assert(stream->rbuf.len != stream->rlen);
        stream->io_watcher.flags &= (~UV_IO_RABLE);
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        {
            uv__stream_read_complete(stream, -errno);
        }
        else
        {
            if (stream->bysome && stream->rlen > 0)
            {
                uv__stream_read_complete(stream, stream->rlen);
            }
            else
            {
                if (uv__io_start(stream->loop, &stream->io_watcher, UV__POLLIN))
                {
                    uv__handle_start(stream);
                }
            }
        }
    }
    else if (n == 0)
    {
        assert(stream->rbuf.len != stream->rlen);
        stream->io_watcher.flags &= (~UV_IO_RABLE);
        stream->flags |= UV_STREAM_READ_EOF;
        if (stream->bysome)
        {
            uv__stream_read_complete(stream, stream->rlen > 0 ? stream->rlen : UV_EOF);
        }
        else
        {
            uv__stream_read_complete(stream, UV_EOF);
        }
    }
    else
    {
        assert(stream->rbuf.len == stream->rlen);
        stream->io_watcher.flags |= UV_IO_RABLE;
        uv__stream_read_complete(stream, stream->rlen);
    }

#if 0
    if (nread > 0)
    {
        //FIXME IPC handle
        if (is_ipc)
        {
            err = uv__stream_recv_cmsg(stream, &msg);
            if (err != 0)
            {
                stream->read_cb(stream, err, &stream->rbuf, stream->data);
                return err;
            }
        }

        stream->read_cb(stream, nread, &stream->rbuf, stream->data);
    }
#endif
}

static void uv__stream_io_e(uv_stream_t* stream)
{
    uv__stream_error(stream, UV_EOF);
}

static void uv__stream_io_r(uv_stream_t* stream)
{
    assert(stream->read_cb);
    uv__stream_read(stream);
}

static void uv__stream_io_w(uv_stream_t* stream)
{
    assert(stream);

    QUEUE* h = (QUEUE*) &stream->write_queue;
    QUEUE* q;
    uv_write_base_t* base = null;

    if ((!stream->connect_req) && QUEUE_EMPTY(h))
    {
        if (uv__io_stop(stream->loop, &stream->io_watcher, UV__POLLOUT) == 0)
        {
            uv__handle_stop(stream);
        }
        return;
    }

    if (stream->connect_req)
    {
        uv__stream_connect(stream);
    }
    else
    {
        assert(uv__stream_fd(stream) >= 0);
        assert(!(stream->flags & UV_STREAM_BLOCKING));

        q = QUEUE_HEAD(h);
        base = QUEUE_DATA(q, uv_write_base_t, queue);
        assert(base->stream == stream);
        base->cb(base, h);
    }
}

static void uv__stream_io(uv_loop_t* loop, uv__io_t* w, unsigned int events)
{
    uv_stream_t* stream = container_of(w, uv_stream_t, io_watcher);

    if (w->reqcnt == 0)
    {
        if (uv__io_stop(stream->loop, &stream->io_watcher, UV__POLLIN | UV__POLLOUT) == 0)
        {
            uv__handle_stop(stream);
        }
        return;
    }

    assert(events);
    assert(stream->type == UV_TCP || stream->type == UV_NAMED_PIPE || stream->type == UV_TTY);
    assert(!(stream->flags & UV_CLOSING) && !(stream->flags & UV_CLOSED));
    assert(uv__stream_fd(stream) >= 0);

    if ((events & UV__POLLOUT) || (events & UV__POLLIN))
    {
        if (w->step == 0) /* now read */
        {
            if (events & UV__POLLIN)
            {
//                fprintf(stderr, "1---->uv__stream_io_r\n");
                w->step = 1; /* next write */
                if (events & UV__POLLOUT)
                {
                    uv__io_start(stream->loop, &stream->io_watcher, UV__POLLOUT);
                }
                uv__stream_io_r(stream);

            }
            else if (events & UV__POLLOUT)
            {
//                fprintf(stderr, "2---->uv__stream_io_w\n");
                w->step = 0; /* next read */
                uv__stream_io_w(stream);
            }
        }
        else /* now write */
        {
            if (events & UV__POLLOUT)
            {
//                fprintf(stderr, "2---->uv__stream_io_w\n");
                w->step = 0; /* next read */
                if (events & UV__POLLIN)
                {
                    uv__io_start(stream->loop, &stream->io_watcher, UV__POLLIN);
                }
                uv__stream_io_w(stream);

            }
            else if (events & UV__POLLIN)
            {
//                fprintf(stderr, "2---->uv__stream_io_r\n");
                w->step = 1; /* next write */
                uv__stream_io_r(stream);
            }
        }
    }
    else if (events)
    {
        uv__stream_io_e(stream);
    }
}

void uv__stream_init(uv_loop_t* loop, uv_stream_t* stream, uv_handle_type type)
{
    uv__handle_init(loop, stream, type);
    stream->read_cb = null;
//    stream->alloc_cb = null;
    stream->connection_cb = null;
    stream->connect_req = null;
    stream->shutdown_req = null;
    stream->accepted_fd = -1;
    stream->queued_fds = null;
    stream->delayed_error = 0;
    QUEUE_INIT(&stream->write_queue);
//    QUEUE_INIT(&stream->write_completed_queue);
    stream->write_queue_size = 0;

//    if (loop->emfile_fd == -1)
//    {
//        err = uv__open_cloexec("/", O_RDONLY);
//        if (err >= 0)
//        {
//            loop->emfile_fd = err;
//        }
//    }

#if defined(__APPLE__)
    stream->select = null;
#endif /* defined(__APPLE_) */

    uv__io_init(&stream->io_watcher, uv__stream_io, -1);
}

void uv_write2(uv_stream_t* stream, const uv_buf_t bufs[], unsigned int nbufs, uv_stream_t* send_handle, uv_write_cb write_cb,
               void* user_data)
{
    assert(nbufs > 0);
    assert((stream->type == UV_TCP || stream->type == UV_NAMED_PIPE || stream->type == UV_TTY) && "uv_write (unix) does not yet support other types of streams");

    if (!nbufs || !bufs || !write_cb)
    {
        write_cb(stream, UV_EINVAL, bufs, nbufs, user_data);
        return;
    }

    uv_write_t* req = calloc(1, sizeof(uv_write_t));
    if (!req)
    {
        write_cb(stream, UV_ENOMEM, bufs, nbufs, user_data);
        return;
    }

    if (uv__stream_fd(stream) < 0)
    {
        free(req);
        req = null;
        write_cb(stream, UV_EBADF, bufs, nbufs, user_data);
        return;
    }

    if (send_handle)
    {
        if (stream->type != UV_NAMED_PIPE || !((uv_pipe_t*) stream)->ipc)
        {
            free(req);
            req = null;
            write_cb(stream, UV_EINVAL, bufs, nbufs, user_data);
            return;
        }

        /* XXX We abuse uv_write2() to send over UDP handles to child processes.
         * Don't call uv__stream_fd() on those handles, it's a macro that on OS X
         * evaluates to a function that operates on a uv_stream_t with a couple of
         * OS X specific fields. On other Unices it does (handle)->io_watcher.fd,
         * which works but only by accident.
         */
        if (uv__handle_fd((uv_handle_t*) send_handle) < 0)
        {
            free(req);
            req = null;
            write_cb(stream, UV_EBADF, bufs, nbufs, user_data);
            return;
        }
    }

    /* Initialize the req */
    uv__req_init(stream->loop, req, UV_WRITE);
    req->data = user_data;
    req->write_cb = write_cb;
    req->base.stream = stream;
    req->error = 0;
    req->send_handle = send_handle;
    req->base.cb = uv__stream_write;
    req->base.cancel_cb = uv__stream_write_complete;
    req->bufs = req->bufsml;
    req->bufs_backup = req->bufsml_backup;

    if (nbufs > ARRAY_SIZE(req->bufsml))
    {
        req->bufs = malloc(nbufs * sizeof(bufs[0]));
        req->bufs_backup = malloc(nbufs * sizeof(bufs[0]));
    }

    if (req->bufs == null || req->bufs_backup == null)
    {
        if (req->bufs && req->bufs != req->bufsml)
        {
            free(req->bufs);
            req->bufs = null;
        }
        if (req->bufs_backup && req->bufs_backup != req->bufsml_backup)
        {
            free(req->bufs_backup);
            req->bufs_backup = null;
        }
        uv__req_unregister(stream->loop, req);

        free(req);
        req = null;
        write_cb(stream, UV_ENOMEM, bufs, nbufs, user_data);
        return;
    }

    memcpy(req->bufs, bufs, nbufs * sizeof(bufs[0]));
    memcpy(req->bufs_backup, bufs, nbufs * sizeof(bufs[0]));
    req->nbufs = nbufs;
    req->write_index = 0;
    stream->write_queue_size += uv__count_bufs(bufs, nbufs);
    stream->io_watcher.reqcnt++;

    /* Append the request to write_queue. */
    QUEUE_INSERT_TAIL(&stream->write_queue, &req->base.queue);

    /*
     * blocking streams should never have anything in the queue.
     * if this assert fires then somehow the blocking stream isn't being
     * sufficiently flushed in uv__write.
     */
    assert(!(stream->flags & UV_STREAM_BLOCKING));

    DEBUG_PRINT();
    if (uv__io_start(stream->loop, &stream->io_watcher, UV__POLLOUT))
    {
        uv__handle_start(stream);
    }
    uv__stream_osx_interrupt_select(stream);
}

/* The buffers to be written must remain valid until the callback is called.
 * This is not required for the uv_buf_t array.
 */
void uv_write(uv_stream_t* stream, const uv_buf_t bufs[], unsigned int nbufs, uv_write_cb write_cb, void* user_data)
{
    uv_write2(stream, bufs, nbufs, null, write_cb, user_data);
}

void uv_sendfile(uv_stream_t* stream, int in_fd, off_t offset, size_t count, uv_sendfile_cb cb, void* user_data)
{
    assert(!stream->connect_req);
    assert((stream->type == UV_TCP || stream->type == UV_NAMED_PIPE || stream->type == UV_TTY) && "uv_write (unix) does not yet support other types of streams");

    if (uv__stream_fd(stream) < 0 || in_fd < 0)
    {
        cb(stream, in_fd, offset, count, UV_EBADF, user_data);
        return;
    }

    uv_sendfile_t* req = calloc(1, sizeof(uv_sendfile_t));
    if (!req)
    {
        cb(stream, in_fd, offset, count, UV_ENOMEM, user_data);
        return;
    }

    uv__nonblock(in_fd, 1);

    /* Initialize the req */
    uv__req_init(stream->loop, req, UV_WRITE);
    req->in_fd = in_fd;
    req->data = user_data;
    req->sendfile_cb = cb;
    req->base.stream = stream;
    req->error = 0;
    req->offset = offset;
    req->size_need_write = count;
    req->size_writed_all = 0;
    req->base.cb = uv__sendfile;
    req->base.cancel_cb = uv__sendfile_complete;

    stream->io_watcher.reqcnt++;
    QUEUE_INIT(&req->base.queue);

    /* Append the request to write_queue. */
    QUEUE_INSERT_TAIL(&stream->write_queue, &req->base.queue);

    /*
     * blocking streams should never have anything in the queue.
     * if this assert fires then somehow the blocking stream isn't being
     * sufficiently flushed in uv__write.
     */
    assert(!(stream->flags & UV_STREAM_BLOCKING));

    DEBUG_PRINT();
    if (uv__io_start(stream->loop, &stream->io_watcher, UV__POLLOUT))
    {
        uv__handle_start(stream);
    }
    uv__stream_osx_interrupt_select(stream);
}

static void uv_readreq(uv_stream_t* stream, uv_buf_t* buf, uv_read_cb read_cb, void* user_data, int bysome)
{
    assert(stream || buf || read_cb || buf->len);
    assert(stream->type == UV_TCP || stream->type == UV_NAMED_PIPE || stream->type == UV_TTY);
    assert(!stream->read_cb);
    assert(!stream->connect_req);
    assert(!(stream->flags & UV_CLOSING));
    assert(!(stream->flags & UV_CLOSED));

    /* The UV_STREAM_READING flag is irrelevant of the state of the tcp - it just
     * expresses the desired state of the user.
     */
//    stream->flags |= UV_STREAM_READING;
    assert(uv__stream_fd(stream) >= 0);

    stream->data = user_data;
    stream->rbuf = *buf;
    stream->rlen = 0;
    stream->read_cb = read_cb;
    stream->bysome = bysome;
    stream->io_watcher.reqcnt++;

    if (uv__io_start(stream->loop, &stream->io_watcher, UV__POLLIN))
    {
        uv__handle_start(stream);
    }
    uv__stream_osx_interrupt_select(stream);
}

void uv_read(uv_stream_t* stream, uv_buf_t* buf, uv_read_cb read_cb, void* user_data)
{
    uv_readreq(stream, buf, read_cb, user_data, 0);
}

void uv_read_some(uv_stream_t* stream, uv_buf_t* buf, uv_read_cb read_cb, void* user_data)
{
    uv_readreq(stream, buf, read_cb, user_data, 1);
}

void uv_read_cancel(uv_stream_t* stream)
{
    if (stream->read_cb)
    {
        stream->read_cb = null;
        stream->io_watcher.reqcnt--;

        if (!uv__io_stop(stream->loop, &stream->io_watcher, UV__POLLIN))
        {
            uv__handle_stop(stream);
        }
    }
}

int uv_is_readable(const uv_stream_t* stream)
{
    return !!(stream->flags & UV_STREAM_READABLE);
}

int uv_is_writable(const uv_stream_t* stream)
{
    return !!(stream->flags & UV_STREAM_WRITABLE);
}

#if defined(__APPLE__)
int uv___stream_fd(const uv_stream_t* handle)
{
    const uv__stream_select_t* s;

    assert(handle->type == UV_TCP ||
            handle->type == UV_TTY ||
            handle->type == UV_NAMED_PIPE);

    s = handle->select;
    if (s != null)
    return s->fd;

    return handle->io_watcher.fd;
}
#endif /* defined(__APPLE__) */

unsigned int uv__stream_close(uv_stream_t* stream)
{
    unsigned int i = 0;
    unsigned int reqcnt = 0;
    uv__stream_queued_fds_t* queued_fds;

    assert(stream->flags & UV_CLOSING);
    assert(!(stream->flags & UV_CLOSED));

    reqcnt = stream->io_watcher.reqcnt;
    if (reqcnt)
    {
        uv__stream_error(stream, UV_ECANCELED);
        return reqcnt;
    }

#if defined(__APPLE__)
    /* Terminate select loop first */
    if (stream->select != null)
    {
        uv__stream_select_t* s;

        s = stream->select;

        uv_sem_post(&s->close_sem);
        uv_sem_post(&s->async_sem);
        uv__stream_osx_interrupt_select(handle);
        uv_thread_join(&s->thread);
        uv_sem_destroy(&s->close_sem);
        uv_sem_destroy(&s->async_sem);
        uv__close(s->fake_fd);
        uv__close(s->int_fd);
        uv_close((uv_handle_t*) &s->async, uv__stream_osx_cb_close);

        stream->select = null;
    }
#endif /* defined(__APPLE__) */

    uv__io_close(stream->loop, &stream->io_watcher);
    assert(!uv__io_active(&stream->io_watcher, UV__POLLIN | UV__POLLOUT));
    assert(QUEUE_EMPTY(&stream->io_watcher.watcher_queue));
    uv__handle_stop(stream);

    if (stream->io_watcher.fd != -1)
    {
        /* Don't close stdio file descriptors.  Nothing good comes from it. */
        if (stream->io_watcher.fd > STDERR_FILENO)
        {
            uv__close(stream->io_watcher.fd);
        }
        stream->io_watcher.fd = -1;
    }

    if (stream->accepted_fd != -1)
    {
        uv__close(stream->accepted_fd);
        stream->accepted_fd = -1;
    }

    /* Close all queued fds */
    if (stream->queued_fds != null)
    {
        queued_fds = stream->queued_fds;
        for (i = 0; i < queued_fds->offset; i++)
        {
            uv__close(queued_fds->fds[i]);
        }
        free(stream->queued_fds);
        stream->queued_fds = null;
    }

//    uv__stream_destroy(handle);

//    assert(!uv__io_active(&handle->io_watcher, UV__POLLIN | UV__POLLOUT));

    return reqcnt;
}

int uv_stream_set_blocking(uv_stream_t* handle, int blocking)
{
    return UV_ENOSYS;
}
