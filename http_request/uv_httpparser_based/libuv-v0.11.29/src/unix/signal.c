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

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//typedef struct
//{
//    uv_signal_t* handle;
//    int signum;
//} uv__signal_msg_t;

struct uv__signalfd_siginfo
{
    uint32_t ssi_signo; /* Signal number */
    int32_t ssi_errno; /* Error number (unused) */
    int32_t ssi_code; /* Signal code */
    uint32_t ssi_pid; /* PID of sender */
    uint32_t ssi_uid; /* Real UID of sender */
    int32_t ssi_fd; /* File descriptor (SIGIO) */
    uint32_t ssi_tid; /* Kernel timer ID (POSIX timers) */
    uint32_t ssi_band; /* Band event (SIGIO) */
    uint32_t ssi_overrun; /* POSIX timer overrun count */
    uint32_t ssi_trapno; /* Trap number that caused signal */
    int32_t ssi_status; /* Exit status or signal (SIGCHLD) */
    int32_t ssi_int; /* Integer sent by sigqueue(3) */
    uint64_t ssi_ptr; /* Pointer sent by sigqueue(3) */
    uint64_t ssi_utime; /* User CPU time consumed (SIGCHLD) */
    uint64_t ssi_stime; /* System CPU time consumed (SIGCHLD) */
    uint64_t ssi_addr; /* Address that generated signal (for hardware-generated signals) */
    uint8_t pad[48]; /* Pad size to 128 bytes (allow for additional fields in the future) */
};

static int uv__signal_compare(uv_signal_t* w1, uv_signal_t* w2);
static void uv__signal_stop(uv_signal_t* handle);

RB_GENERATE_STATIC(uv__signal_tree_s, uv_signal_s, tree_entry, uv__signal_compare)

#if 0
static void uv__signal_global_init(void)
{
    if (uv__make_pipe(uv__signal_lock_pipefd, 0))
    abort();

    if (uv__signal_unlock())
    abort();
}

void uv__signal_global_once_init(void)
{
    pthread_once(&uv__signal_global_init_guard, uv__signal_global_init);
}
#endif

static uv_signal_t* uv__signal_first_handle(struct uv__signal_tree_s* signal_tree, int signum)
{
    /* This function must be called with the signal lock held. */
    uv_signal_t lookup;
    uv_signal_t* handle;

    lookup.signum = signum;
    lookup.loop = null;

    handle = RB_NFIND(uv__signal_tree_s, signal_tree, &lookup);

    if (handle != null && handle->signum == signum)
    {
        return handle;
    }

    return null;
}

void uv__signalfd_cb(uv_poll_t* handle, int status, int events, void* user_data)
{
    int fd = *((int*) user_data);
    struct uv__signalfd_siginfo siginfo;
    uv_buf_t buf;
    ssize_t s = 0;
    uv_signal_t* shandle = null;

    ASSERT(status == 0);
    ASSERT(events & UV_READABLE);

    buf.base = (char*) &siginfo;
    buf.len = sizeof(siginfo);

    memset(buf.base, 0, buf.len);

    s = uvread(fd, &buf, &s);

    ASSERT(s == sizeof(siginfo));


    for (shandle = uv__signal_first_handle(&handle->loop->signal_tree, siginfo.ssi_signo);
         shandle != null && shandle->signum == siginfo.ssi_signo;
         shandle = RB_NEXT(uv__signal_tree_s, &handle->loop->signal_tree, shandle))
    {
        shandle->caught_signals++;
        shandle->signal_cb(shandle, shandle->signum, shandle->data);
        break;
    }
}

int uv__signalfd_init(uv_loop_t* loop)
{
    ASSERT(sizeof(struct uv__signalfd_siginfo) == 128);
    ASSERT(loop);

    sigemptyset(&loop->mask);

    loop->signal_fd = uv_signalfd(-1, &loop->mask);
    if (loop->signal_fd == -1)
    {
        return -errno;
    }

    return 0;
}

#if 0
static int uv__signal_lock(void)
{
    int r;
    char data;

    do
    {
        r = read(uv__signal_lock_pipefd[0], &data, sizeof data);
    }while (r < 0 && errno == EINTR);

    return (r < 0) ? -1 : 0;
}

static int uv__signal_unlock(void)
{
    int r;
    char data = 42;

    do
    {
        r = write(uv__signal_lock_pipefd[1], &data, sizeof data);
    }while (r < 0 && errno == EINTR);

    return (r < 0) ? -1 : 0;
}

static void uv__signal_block_and_lock(sigset_t* saved_sigmask)
{
    sigset_t new_mask;

    if (sigfillset(&new_mask))
    abort();

    if (pthread_sigmask(SIG_SETMASK, &new_mask, saved_sigmask))
    abort();

    if (uv__signal_lock())
    abort();
}

static void uv__signal_unlock_and_unblock(sigset_t* saved_sigmask)
{
    if (uv__signal_unlock())
    abort();

    if (pthread_sigmask(SIG_SETMASK, saved_sigmask, null))
    abort();
}

static void uv__signal_handler(int signum)
{
    uv__signal_msg_t msg;
    uv_signal_t* handle;
    int saved_errno;

    saved_errno = errno;
    memset(&msg, 0, sizeof msg);

    if (uv__signal_lock())
    {
        errno = saved_errno;
        return;
    }

    for (handle = uv__signal_first_handle(signum); handle != null && handle->signum == signum;
            handle = RB_NEXT(uv__signal_tree_s, &uv__signal_tree, handle))
    {
        int r;

        msg.signum = signum;
        msg.handle = handle;

        /* write() should be atomic for small data chunks, so the entire message
         * should be written at once. In theory the pipe could become full, in
         * which case the user is out of luck.
         */
        do
        {
            r = write(handle->loop->signal_pipefd[1], &msg, sizeof msg);
        }while (r == -1 && errno == EINTR);

        ASSERT(r == sizeof msg || (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)));

        if (r != -1)
        handle->caught_signals++;
    }

    uv__signal_unlock();
    errno = saved_errno;
}
#endif

static int uv__signal_register_handler(uv_loop_t* loop, int signum)
{
    sigaddset(&loop->mask, signum);

    if (sigprocmask(SIG_SETMASK, null, null) == -1)
    {
        return -errno;
    }

    if (sigprocmask(SIG_BLOCK, &loop->mask, null) == -1)
    {
        return -errno;
    }

    loop->signal_fd = uv_signalfd(loop->signal_fd, &loop->mask);
    if (loop->signal_fd == -1)
    {
        return -errno;
    }

    return 0;
}

static int uv__signal_unregister_handler(uv_loop_t* loop, int signum)
{
    sigdelset(&loop->mask, signum);

    if (sigprocmask(SIG_SETMASK, null, null) == -1)
    {
        return -errno;
    }

    if (sigprocmask(SIG_BLOCK, &loop->mask, null) == -1)
    {
        return -errno;
    }

    loop->signal_fd = uv_signalfd(loop->signal_fd, &loop->mask);
    if (loop->signal_fd == -1)
    {
        return -errno;
    }

    return 0;
}

#if 0
static int uv__signal_loop_once_init(uv_loop_t* loop)
{
    int err;

    /* Return if already initialized. */
    if (loop->signal_pipefd[0] != -1)
    {
        return 0;
    }

    err = uv__make_pipe(loop->signal_pipefd, UV__F_NONBLOCK);
    if (err)
    {
        return err;
    }

    uv__io_init(&loop->signal_io_watcher, uv__signal_event, loop->signal_pipefd[0]);
    DEBUG_PRINT();
    uv__io_start(loop, &loop->signal_io_watcher, UV__POLLIN);

    return 0;
}

void uv__signal_loop_cleanup(uv_loop_t* loop)
{
    QUEUE* q;

    /* Stop all the signal watchers that are still attached to this loop. This
     * ensures that the (shared) signal tree doesn't contain any invalid entries
     * entries, and that signal handlers are removed when appropriate.
     */
    QUEUE_FOREACH(q, &loop->handle_queue)
    {
        uv_handle_t* handle = QUEUE_DATA(q, uv_handle_t, handle_queue);

        if (handle->type == UV_SIGNAL)
        uv__signal_stop((uv_signal_t*) handle);
    }

    if (loop->signal_pipefd[0] != -1)
    {
        uv__close(loop->signal_pipefd[0]);
        loop->signal_pipefd[0] = -1;
    }

    if (loop->signal_pipefd[1] != -1)
    {
        uv__close(loop->signal_pipefd[1]);
        loop->signal_pipefd[1] = -1;
    }
}
#endif

int uv_signal_init(uv_loop_t* loop, uv_signal_t* handle)
{
    ASSERT(loop->signal_fd > 0);

    uv__handle_init(loop, (uv_handle_t* ) handle, UV_SIGNAL);
    uv__handle_start(handle);
    handle->signum = 0;
    handle->caught_signals = 0;
    handle->dispatched_signals = 0;

    return 0;
}

void uv_signal_start(uv_signal_t* handle, uv_signal_cb signal_cb, int signum, void* user_data)
{
    int err;

    ASSERT(handle->loop);
    ASSERT(signum != 0);
    ASSERT(!(handle->flags & (UV_CLOSING | UV_CLOSED)));

    handle->data = user_data;

    /* Short circuit: if the signal watcher is already watching {signum} don't
     * go through the process of deregistering and registering the handler.
     * Additionally, this avoids pending signals getting lost in the small time
     * time frame that handle->signum == 0.
     */
    if (signum == handle->signum)
    {
        handle->signal_cb = signal_cb;
        return;
    }

    /* If the signal handler was already active, stop it first. */
    if (handle->signum != 0)
    {
        uv__signal_stop(handle);
    }

    /* If at this point there are no active signal watchers for this signum (in
     * any of the loops), it's time to try and register a handler for it here.
     */
    if (uv__signal_first_handle(&handle->loop->signal_tree, signum) == null)
    {
        err = uv__signal_register_handler(handle->loop, signum);
        if (err)
        {
            return;
        }
    }

    handle->signum = signum;
    handle->signal_cb = signal_cb;
    RB_INSERT(uv__signal_tree_s, &handle->loop->signal_tree, handle);

    uv__handle_start(handle);
}

#if 0
static void uv__signal_event(uv_loop_t* loop, uv__io_t* w, unsigned int events)
{
    uv__signal_msg_t* msg;
    uv_signal_t* handle;
    char buf[sizeof(uv__signal_msg_t) * 32];
    size_t bytes, end, i;
    int r;

    bytes = 0;
    end = 0;

    do
    {
        r = read(loop->signal_pipefd[0], buf + bytes, sizeof(buf) - bytes);

        if (r == -1 && errno == EINTR)
        continue;

        if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            /* If there are bytes in the buffer already (which really is extremely
             * unlikely if possible at all) we can't exit the function here. We'll
             * spin until more bytes are read instead.
             */
            if (bytes > 0)
            continue;

            /* Otherwise, there was nothing there. */
            return;
        }

        /* Other errors really should never happen. */
        if (r == -1)
        abort();

        bytes += r;

        /* `end` is rounded down to a multiple of sizeof(uv__signal_msg_t). */
        end = (bytes / sizeof(uv__signal_msg_t)) * sizeof(uv__signal_msg_t);

        for (i = 0; i < end; i += sizeof(uv__signal_msg_t))
        {
            msg = (uv__signal_msg_t*) (buf + i);
            handle = msg->handle;

            if (msg->signum == handle->signum)
            {
                ASSERT(!(handle->flags & UV_CLOSING));
                handle->signal_cb(handle, handle->signum, handle->data);
            }

            handle->dispatched_signals++;

            /* If uv_close was called while there were caught signals that were not
             * yet dispatched, the uv__finish_close was deferred. Make close pending
             * now if this has happened.
             */
//            if ((handle->flags & UV_CLOSING) && (handle->caught_signals == handle->dispatched_signals))
//            {
//                uv__make_close_pending((uv_handle_t*) handle);
//            }
        }

        bytes -= end;

        /* If there are any "partial" messages left, move them to the start of the
         * the buffer, and spin. This should not happen.
         */
        if (bytes)
        {
            memmove(buf, buf + end, bytes);
            continue;
        }
    }while (end == sizeof buf);
}
#endif

static int uv__signal_compare(uv_signal_t* w1, uv_signal_t* w2)
{
    /* Compare signums first so all watchers with the same signnum end up
     * adjacent.
     */
    if (w1->signum < w2->signum)
        return -1;
    if (w1->signum > w2->signum)
        return 1;

    /* Sort by loop pointer, so we can easily look up the first item after
     * { .signum = x, .loop = null }.
     */
    if (w1->loop < w2->loop)
        return -1;
    if (w1->loop > w2->loop)
        return 1;

    if (w1 < w2)
        return -1;
    if (w1 > w2)
        return 1;

    return 0;
}

static void uv__signal_stop(uv_signal_t* handle)
{
    uv_signal_t* removed_handle;

    /* If the watcher wasn't started, this is a no-op. */
    if (handle->signum == 0)
    {
        return;
    }

//    uv__signal_block_and_lock(&saved_sigmask);

    removed_handle = RB_REMOVE(uv__signal_tree_s, &handle->loop->signal_tree, handle);
    ASSERT(removed_handle == handle);
    (void) removed_handle;

    /* Check if there are other active signal watchers observing this signal. If
     * not, unregister the signal handler.
     */
    if (uv__signal_first_handle(&handle->loop->signal_tree, handle->signum) == null)
    {
        uv__signal_unregister_handler(handle->loop, handle->signum);
    }

//    uv__signal_unlock_and_unblock(&saved_sigmask);

    handle->signum = 0;
    uv__handle_stop(handle);
}

int uv_signal_stop(uv_signal_t* handle)
{
    ASSERT(!(handle->flags & (UV_CLOSING | UV_CLOSED)));
    uv__signal_stop(handle);
    return 0;
}

void uv__signal_close(uv_signal_t* handle)
{
    uv__signal_stop(handle);
}
