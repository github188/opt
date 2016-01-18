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

/*
 * This file is private to libuv. It provides common functionality to both
 * Windows and Unix backends.
 */

#ifndef UV_COMMON_H_
#define UV_COMMON_H_

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "uv.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

enum {
  UV__HANDLE_INTERNAL = 0x8000,
  UV__HANDLE_ACTIVE   = 0x4000,
  UV__HANDLE_REF      = 0x2000,
  UV__HANDLE_CLOSING  = 0 /* no-op on unix */
};

int uv__tcp_bind(uv_tcp_t* tcp,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 unsigned int flags);

int uv__tcp_connect(uv_connect_t* req,
                    uv_tcp_t* handle,
                    const struct sockaddr* addr,
                    unsigned int addrlen,
                    uv_connect_cb cb,
                    void* user_data);

int uv__udp_bind(uv_udp_t* handle,
                 const struct sockaddr* addr,
                 unsigned int  addrlen,
                 unsigned int flags);

int uv__udp_send(uv_udp_t* handle,
                 uv_udp_send_t* req,
                 const uv_buf_t bufs[],
                 unsigned int nbufs,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 uv_udp_send_cb send_cb,
                 void* user_data);

//int uv__udp_try_send(uv_udp_t* handle,
//                     const uv_buf_t bufs[],
//                     unsigned int nbufs,
//                     const struct sockaddr* addr,
//                     unsigned int addrlen);

//void uv__udp_recv_stop(uv_udp_t* handle);
void uv__udp_recv(uv_udp_t* handle, uv_buf_t* buf, uv_udp_recv_cb recv_cb, void* user_data);

void uv__fs_poll_close(uv_fs_poll_t* handle);

int uv__getaddrinfo_translate_error(int sys_err);    /* EAI_* error. */

void uv__work_submit(uv_loop_t* loop,
                     struct uv__work *w,
                     void (*work)(struct uv__work *w),
                     void (*done)(struct uv__work *w, int status));

void uv__work_done(uv_async_t* handle);

size_t uv__count_bufs(const uv_buf_t bufs[], unsigned int nbufs);

int uv__socket_sockopt(uv_handle_t* handle, int optname, int* value);

void uv__fs_readdir_cleanup(uv_fs_t* req);

#define uv__has_active_reqs(loop)                                             \
  (QUEUE_EMPTY(&(loop)->active_reqs) == 0)

#define uv__req_register(loop, req)                                           \
  do {                                                                        \
    QUEUE_INSERT_TAIL(&(loop)->active_reqs, &(req)->active_queue);            \
  }                                                                           \
  while (0)

#define uv__req_unregister(loop, req)                                         \
  do                                                                          \
  {                                                                           \
      ASSERT(uv__has_active_reqs(loop));                                      \
      QUEUE_REMOVE(&(req)->active_queue);                                     \
  }                                                                           \
  while (0)

#define uv__has_active_handles(loop)                                          \
  ((loop)->active_handles > 0)

#define uv__active_handle_add(h)                                              \
  do                                                                          \
  {                                                                           \
      (h)->loop->active_handles++;                                            \
  } while (0)

#define uv__active_handle_sub(h)                                              \
  do                                                                          \
  {                                                                           \
      (h)->loop->active_handles--;                                            \
  } while (0)

#define uv__is_active(h)                                                      \
  (((h)->flags & UV__HANDLE_ACTIVE) != 0)

#define uv__is_closing(h)                                                     \
  (((h)->flags & (UV_CLOSING |  UV_CLOSED)) != 0)

#define uv__handle_start(h)                                                   \
  do                                                                          \
  {                                                                           \
      ASSERT(!((h)->flags & UV__HANDLE_CLOSING));                             \
      (h)->flags |= UV__HANDLE_ACTIVE;                                        \
  } while (0)

#define uv__handle_stop(h)                                                    \
  do                                                                          \
  {                                                                           \
      ASSERT(!((h)->flags & UV__HANDLE_CLOSING));                             \
      (h)->flags &= ~UV__HANDLE_ACTIVE;                                       \
  } while (0)

#define uv__handle_ref(h)                                                     \
  do                                                                          \
  {                                                                           \
      if (((h)->flags & UV__HANDLE_REF) != 0)                                 \
      {                                                                       \
          break;                                                              \
      }                                                                       \
      (h)->flags |= UV__HANDLE_REF;                                           \
      if (((h)->flags & UV__HANDLE_CLOSING) != 0)                             \
      {                                                                       \
        break;                                                                \
      }                                                                       \
      if (((h)->flags & UV__HANDLE_ACTIVE) != 0)                              \
      {                                                                       \
          uv__active_handle_add(h);                                           \
      }                                                                       \
    } while (0)

#define uv__handle_unref(h)                                                   \
  do                                                                          \
  {                                                                           \
      if (((h)->flags & UV__HANDLE_REF) == 0)                                 \
      {                                                                       \
          break;                                                              \
      }                                                                       \
      (h)->flags &= ~UV__HANDLE_REF;                                          \
      if (((h)->flags & UV__HANDLE_CLOSING) != 0)                             \
      {                                                                       \
          break;                                                              \
      }                                                                       \
      if (((h)->flags & UV__HANDLE_ACTIVE) != 0)                              \
      {                                                                       \
          uv__active_handle_sub(h);                                           \
      }                                                                       \
  } while (0)

#define uv__has_ref(h)                                                        \
  (((h)->flags & UV__HANDLE_REF) != 0)

#define uv__handle_init(loop_, h, type_)                                      \
  do                                                                          \
  {                                                                           \
      (h)->loop = (loop_);                                                    \
      (h)->type = (type_);                                                    \
      (h)->flags = UV__HANDLE_REF;  /* Ref the loop when active. */           \
      QUEUE_INSERT_TAIL(&(loop_)->handle_queue, &(h)->handle_queue);          \
      uv__active_handle_add(h);                                               \
  } while (0)

#define uv__handle_close(h)                                                   \
  do                                                                          \
  {                                                                           \
      uv__active_handle_sub(h);                                               \
      QUEUE_REMOVE(&(h)->handle_queue);                                       \
      (h)->flags &= ~UV__HANDLE_REF;                                          \
      (h)->loop = null;                                                       \
      (h)->type = 0;                                                          \
  } while (0)

#endif /* UV_COMMON_H_ */
