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

#include <unistd.h>
#include <assert.h>
#include <errno.h>

static void uv__poll_io(uv_loop_t* loop, uv__io_t* w, unsigned int events)
{
    uv_poll_t* handle = null;
    int pevents = 0;

    handle = container_of(w, uv_poll_t, io_watcher);

    if (events & UV__POLLERR)
    {
        uv__io_stop(loop, w, UV__POLLIN | UV__POLLOUT);
        uv__handle_stop(handle);
        handle->poll_cb(handle, -EBADF, 0, handle->data);
        return;
    }

    pevents = 0;
    if (events & UV__POLLIN)
    {
        pevents |= UV_READABLE;
    }
    if (events & UV__POLLOUT)
    {
        pevents |= UV_WRITABLE;
    }

    handle->poll_cb(handle, 0, pevents, handle->data);
}

int uv_poll_init(uv_loop_t* loop, uv_poll_t* handle, int fd)
{
    uv__nonblock(fd, 1);
    uv__handle_init(loop, (uv_handle_t* ) handle, UV_POLL);
    uv__io_init(&handle->io_watcher, uv__poll_io, fd);
    handle->poll_cb = null;
    return 0;
}

int uv_poll_init_socket(uv_loop_t* loop, uv_poll_t* handle, uv_os_sock_t socket)
{
    return uv_poll_init(loop, handle, socket);
}

static void uv__poll_stop(uv_poll_t* handle)
{
    uv__io_stop(handle->loop, &handle->io_watcher, UV__POLLIN | UV__POLLOUT);
    uv__handle_stop(handle);
}

int uv_poll_stop(uv_poll_t* handle)
{
    assert(!(handle->flags & (UV_CLOSING | UV_CLOSED)));
    uv__poll_stop(handle);
    return 0;
}

void uv_poll_start(uv_poll_t* handle, int pevents, uv_poll_cb poll_cb, void* user_data)
{
    int events;

    ASSERT(pevents != 0);
    ASSERT((pevents & ~(UV_READABLE | UV_WRITABLE)) == 0);
    ASSERT(!(handle->flags & (UV_CLOSING | UV_CLOSED)));

    handle->data = user_data;
    uv__poll_stop(handle);

    events = 0;
    if (pevents & UV_READABLE)
    {
        events |= UV__POLLIN;
    }
    if (pevents & UV_WRITABLE)
    {
        events |= UV__POLLOUT;
    }

    DEBUG_PRINT();
    if (uv__io_start(handle->loop, &handle->io_watcher, events))
    {
        uv__handle_start(handle);
    }
    handle->poll_cb = poll_cb;
}

void uv__poll_close(uv_poll_t* handle)
{
    uv__poll_stop(handle);
}
