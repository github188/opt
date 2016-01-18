/*
 * uv_conn.c
 *
 *  Created on: 2014-6-20
 *      Author: wang.guilin
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "uv.h"

typedef struct uv_send_args_s
{
    uv_conn_send_cb send_cb;
    void* user_data;
    QUEUE queue;
} uv_send_args_t;

static void uv_conn_status_call(uv_handle_t* handle)
{
    uv_conn_t* conn = container_of(handle, uv_conn_t, uv);
    conn->scb(conn, conn->result, conn->user_data);
}

static int uv_conn_cache_sockname(uv_conn_t* conn)
{
    int res = 0;
    int len = sizeof(conn->sockname);
    char ip[64] = "";
    uint16_t port;

    res = uv_tcp_getsockname(&conn->uv.tcp, &conn->sockname.addr, &len);
    if (!res)
    {
        uv_ip_name(&conn->sockname, ip, sizeof(ip));
        port = conn->sockname.addr.sa_family == AF_INET6 ? ntohs(conn->sockname.addr6.sin6_port) :
                                                           ntohs(conn->sockname.addr4.sin_port);
        snprintf(conn->sockaddr, sizeof(conn->sockaddr), "%s:%u", ip, port);
    }

    return res;
}

static int uv_conn_cache_peername(uv_conn_t* conn)
{
    int res = 0;
    int len = sizeof(conn->peername);
    char ip[64] = "";
    uint16_t port;

    res = uv_tcp_getpeername(&conn->uv.tcp, &conn->peername.addr, &len);
    if (!res)
    {
        uv_ip_name(&conn->peername, ip, sizeof(ip));
        port = conn->peername.addr.sa_family == AF_INET6 ? ntohs(conn->peername.addr6.sin6_port) :
                                                           ntohs(conn->peername.addr4.sin_port);
        snprintf(conn->peeraddr, sizeof(conn->peeraddr), "%s:%u", ip, port);
    }

    return res;
}

static void uv_conn_connect_done(uv_tcp_t* handle, int status, void* user_data)
{
    uv_conn_t* conn = container_of(handle, uv_conn_t, uv);
    conn->result = status;

    if (!conn->result)
    {
        conn->result = uv_conn_cache_sockname(conn);
    }

    uv_conn_status_call(&conn->uv.handle);
}

static void uv_conn_do_connect(uv_conn_t* conn, const struct sockaddr* addr)
{
    uv_tcp_connect(&conn->uv.tcp, addr, uv_conn_connect_done, null);
}

static void uv_conn_on_getaddrinfo(uv_getaddrinfo_t* req, int status, struct addrinfo* res, void* user_data)
{
    uv_conn_t* conn = user_data;
    conn->result = status;

    free(conn->resolver);
    conn->resolver = null;

    if (conn->result)
    {
        uv_conn_status_call(&conn->uv.handle);
    }
    else
    {
        conn->addr = (*((uv_addr_t*) res->ai_addr));
        uv_conn_do_connect(conn, &conn->addr.addr);
    }
}

static void uv_conn_do_resolve(uv_conn_t* conn, const char* host, const int port)
{
    conn->resolver = calloc(1, sizeof(uv_getaddrinfo_t));
    if (!conn->resolver)
    {
        conn->result = UV_ENOMEM;
        uv_conn_status_call(&conn->uv.handle);
    }
    else
    {
        uv_getaddrinfo(conn->loop, conn->resolver, host, port, uv_conn_on_getaddrinfo, conn);
    }
}

static void uv_conn_tcp_on_recv(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf, void* user_data)
{
    uv_conn_t* conn = container_of(handle, uv_conn_t, uv);
    conn->rcb(conn, null, nread, buf, user_data);
}

static void uv_conn_udp_on_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr,
                                unsigned flags, void* user_data)
{
    uv_conn_t* conn = container_of(handle, uv_conn_t, uv);
    conn->rcb(conn, addr, nread, buf, user_data);
}

static uv_send_args_t* uv_conn_new_send_args(uv_conn_t* conn)
{
    QUEUE* q = null;
    uv_send_args_t* send_args = null;

    if (!QUEUE_EMPTY(&conn->send_args_queue))
    {
        q = QUEUE_HEAD(&conn->send_args_queue);
        QUEUE_REMOVE(q);
        QUEUE_INIT(q);
        send_args = QUEUE_DATA(q, uv_send_args_t, queue);
    }
    else
    {
        send_args = calloc(1, sizeof(uv_send_args_t));
    }

    return send_args;
}

static void uv_conn_clean_send_args(uv_conn_t* conn)
{
    QUEUE* q = null;
    uv_send_args_t* send_args = null;

    while (!QUEUE_EMPTY(&conn->send_args_queue))
    {

        q = QUEUE_HEAD(&conn->send_args_queue);
        QUEUE_REMOVE(q);
        QUEUE_INIT(q);
        send_args = QUEUE_DATA(q, uv_send_args_t, queue);
        assert(send_args);
        free(send_args);
    }
}

static void uv_conn_tcp_on_send(uv_stream_t* handle, int status, const uv_buf_t bufs[], unsigned int nbufs, void* user_data)
{
    uv_conn_t* conn = container_of(handle, uv_conn_t, uv);
    uv_send_args_t* send_args = user_data;
    QUEUE_INSERT_TAIL(&conn->send_args_queue, &send_args->queue);
    send_args->send_cb(conn, null, bufs, nbufs, status, send_args->user_data);
}

static void uv_conn_udp_on_send(uv_udp_t* handle, int status, const uv_buf_t bufs[], unsigned int nbufs,
                                const struct sockaddr* addr, void* user_data)
{
    uv_conn_t* conn = container_of(handle, uv_conn_t, uv);
    uv_send_args_t* send_args = user_data;
    QUEUE_INSERT_TAIL(&conn->send_args_queue, &send_args->queue);
    send_args->send_cb(conn, null, bufs, nbufs, status, send_args->user_data);
}

void uv_conn_init(uv_loop_t* loop, uv_conn_t* conn)
{
    conn->loop = loop;
    conn->type = uvct_none;
    QUEUE_INIT(&conn->send_args_queue);
}

int uv_conn_close(uv_conn_t* conn)
{
    int rc = 0;
    assert(conn);

    if (conn->type != uvct_none)
    {
        if (conn->resolver)
        {
            uv_getaddrinfo_cancel(conn->resolver);
            free(conn->resolver);
            conn->resolver = null;
        }
        rc = uv_close(&conn->uv.handle);
        if (rc == 0) /* close done */
        {
            uv_conn_clean_send_args(conn);
            conn->type = uvct_none;
        }
    }
    return rc;
}

void uv_conn_accept_from(uv_conn_t* conn, uv_stream_t* server, uv_conn_status_cb cb, void* user_data)
{
    assert(conn);
    assert(conn->type == uvct_none);
    assert(server);

    conn->type = uvct_tcp;

    conn->scb = cb;
    conn->user_data = user_data;

    uv_tcp_init(conn->loop, &conn->uv.tcp);

    do
    {

        conn->result = uv_accept(server, &conn->uv.stream);
        if (conn->result)
        {
            break;
        }

        conn->result = uv_conn_cache_sockname(conn);
        if (conn->result)
        {
            break;
        }

        conn->result = uv_conn_cache_peername(conn);
        if (conn->result)
        {
            break;
        }
    } while (0);

    uv_conn_status_call(&conn->uv.handle);
}

void uv_conn_connect_to(uv_conn_t* conn, const char* host, int port, uv_conn_status_cb cb, void* user_data)
{
    assert(conn);
    assert(conn->type == uvct_none);

    conn->type = uvct_tcp;
    conn->scb = cb;
    conn->user_data = user_data;

    uv_tcp_init(conn->loop, &conn->uv.tcp);

    snprintf(conn->peeraddr, sizeof(conn->peeraddr), "%s:%u", host ? host : "", port);

    conn->result = uv_ip_addr(host, port, &conn->addr);
    if (conn->result)
    {
        uv_conn_do_resolve(conn, host, port);
    }
    else
    {
        uv_conn_do_connect(conn, &conn->addr.addr);
    }
}

void uv_conn_bind_socket(uv_conn_t* conn, int socketfd, uv_conn_status_cb cb, void* user_data)
{
    assert(conn);
    assert(conn->type == uvct_none);
    assert(socketfd >= 0);

    conn->type = uvct_tcp;

    conn->scb = cb;
    conn->user_data = user_data;

    uv_tcp_init(conn->loop, &conn->uv.tcp);

    do
    {
        conn->result = uv_tcp_open(&conn->uv.tcp, socketfd);
        if (conn->result)
        {
            break;
        }

        conn->result = uv_conn_cache_sockname(conn);
        if (conn->result)
        {
            break;
        }

        conn->result = uv_conn_cache_peername(conn);
    } while (0);

    uv_conn_status_call(&conn->uv.handle);
}

void uv_conn_bind_udp(uv_conn_t* conn, const char* ip, const uint16_t port, uv_conn_status_cb cb, void* user_data)
{
    assert(conn);
    assert(conn->type == uvct_none);
    assert(ip);
    assert(port);

    conn->type = uvct_udp;

    conn->scb = cb;
    conn->user_data = user_data;

    uv_udp_init(conn->loop, &conn->uv.udp);

    conn->result = uv_ip_addr(ip, port, &conn->addr);
    if (!conn->result)
    {
        conn->result = uv_udp_bind(&conn->uv.udp, &conn->addr, 0);
    }
    uv_conn_status_call(&conn->uv.handle);
}

int uv_conn_tcp_nodelay(uv_conn_t* conn, int enable)
{
    conn->result = uv_tcp_nodelay(&conn->uv.tcp, enable);
    return conn->result;
}

int uv_conn_tcp_keepalive(uv_conn_t* conn, int enable, unsigned int delay)
{
    conn->result = uv_tcp_keepalive(&conn->uv.tcp, enable, delay);
    return conn->result;
}

void uv_conn_recv(uv_conn_t* conn, uv_buf_t* buf, uv_conn_recv_cb recv_cb, void* user_data)
{
    assert(conn);
    assert(conn->type != uvct_none);
    assert(buf && recv_cb);

    conn->rcb = recv_cb;

    switch (conn->type)
    {
    case uvct_tcp:
        uv_read(&conn->uv.stream, buf, uv_conn_tcp_on_recv, user_data);
        break;
    case uvct_udp:
        uv_udp_recv(&conn->uv.udp, buf, uv_conn_udp_on_recv, user_data);
        break;
    default:
        UNREACHABLE();
        break;
    }
}

void uv_conn_recv_some(uv_conn_t* conn, uv_buf_t* buf, uv_conn_recv_cb recv_cb, void* user_data)
{
    assert(conn);
    assert(conn->type != uvct_none);
    assert(buf && recv_cb);

    conn->rcb = recv_cb;

    switch (conn->type)
    {
    case uvct_tcp:
        uv_read_some(&conn->uv.stream, buf, uv_conn_tcp_on_recv, user_data);
        break;
    case uvct_udp:
        uv_udp_recv(&conn->uv.udp, buf, uv_conn_udp_on_recv, user_data);
        break;
    default:
        UNREACHABLE();
        break;
    }
}

void uv_conn_send(uv_conn_t* conn, const struct sockaddr* addr, const uv_buf_t bufs[], uint32_t nbufs, uv_conn_send_cb send_cb,
                  void* user_data)
{
    uv_send_args_t* send_args = null;
    assert(conn);
    assert(conn->type != uvct_none);
    assert(bufs);
    assert(nbufs);
    assert(send_cb);

    send_args = uv_conn_new_send_args(conn);
    assert(send_args);

    send_args->send_cb = send_cb;
    send_args->user_data = user_data;

    switch (conn->type)
    {
    case uvct_tcp:
        uv_write(&conn->uv.stream, bufs, nbufs, uv_conn_tcp_on_send, send_args);
        break;
    case uvct_udp:
        assert(addr);
        uv_udp_send(&conn->uv.udp, bufs, nbufs, addr, uv_conn_udp_on_send, send_args);
        break;
    default:
        UNREACHABLE();
        break;
    }
}

uint32_t uv_conn_send_size(uv_conn_t* conn)
{
    uint32_t size = 0;
    switch (conn->type)
    {
    case uvct_tcp:
        size = conn->uv.tcp.write_queue_size;
        break;
    case uvct_udp:
        size = conn->uv.udp.send_queue_size;
        break;
    default:
        UNREACHABLE();
        break;
    }
    return size;
}

uv_addr_t* uv_conn_sockname(uv_conn_t* conn)
{
    assert(conn);
    return &conn->sockname;
}

uv_addr_t* uv_conn_peername(uv_conn_t* conn)
{
    assert(conn);
    return &conn->peername;
}

const char* uv_conn_sockaddr(uv_conn_t* conn)
{
    assert(conn);
    return conn->sockaddr;
}

const char* uv_conn_peeraddr(uv_conn_t* conn)
{
    assert(conn);
    return conn->peeraddr;
}
