#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uv.h"

//#define UV_HTTP_REQUEST_DEBUG
#ifdef UV_HTTP_REQUEST_DEBUG
#  define UV_HTTP_REQUEST_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#  define UV_HTTP_REQUEST_LOG(...)
#endif

#define UV_HTTP_REQUEST_TIMEOUT 60000

static int http_parse_on_status(http_parser* parser, const char *at, size_t length)
{
    uv_http_request_t* handle = parser->data;

    if (handle->callbacks.onstatus)
    {

        handle->callbacks.onstatus(handle, at, length, handle->user_data);
    }

    return 0;
}

static int http_parse_onbody(http_parser* parser, const char *at, size_t length)
{
    uv_http_request_t* handle = parser->data;

    if (handle->callbacks.onbody)
    {

        handle->callbacks.onbody(handle, at, length, handle->user_data);
    }

    return 0;
}

static int http_parse_on_message_begin(http_parser* parser)
{
    uv_http_request_t* handle = parser->data;

    if (handle->callbacks.on_message_begin)
    {

        handle->callbacks.on_message_begin(handle, handle->user_data);
    }

    return 0;
}

static int http_parse_on_message_complete(http_parser* parser)
{
    uv_http_request_t* handle = parser->data;

    if (handle->callbacks.on_message_complete)
    {

        handle->callbacks.on_message_complete(handle, handle->user_data);
    }

    return 0;
}

static int http_parse_on_header_complete(http_parser* parser)
{
    uv_http_request_t* handle = parser->data;

    if (handle->kv)
    {

        uv_http_header_add(&handle->headers_queue, handle->kv);
        handle->kv = null;
    }

    if (handle->callbacks.on_header_complete)
    {

        handle->callbacks.on_header_complete(handle, handle->user_data);
    }

    return 0;
}

static int http_parse_on_header_field(http_parser* parser, const char* at, size_t len)
{
    uv_http_request_t* handle = (uv_http_request_t*) parser->data;

    if (!handle->kv)
    {

        handle->kv = calloc(1, sizeof(uv_http_keyval_t));
        assert(handle->kv);
    }

    if (!handle->kv->key)
    {

        handle->kv->key = sdsnewlen(at, len);
    }
    else
    {

        if (!handle->kv->value)
        {

            handle->kv->key = sdscatlen(handle->kv->key, at, len);
        }
        else
        {

            uv_http_header_add(&handle->headers_queue, handle->kv);
            handle->kv = null;

            return http_parse_on_header_field(parser, at, len);
        }
    }

    return 0;
}

static int http_parse_on_header_value(http_parser *parser, const char *at, size_t len)
{

    uv_http_request_t *handle = (uv_http_request_t*) parser->data;
    assert(handle->kv);

    if (!handle->kv->value)
    {

        handle->kv->value = sdsnewlen(at, len);
    }
    else
    {

        handle->kv->value = sdscatlen(handle->kv->value, at, len);
    }

    return 0;
}

/*****************************************/

static void uv_uv_http_request_recv(uv_http_request_t* handle);

static void uv_http_request_timeout(uv_timer_t* timer)
{

    uv_http_request_t* handle = container_of(timer, uv_http_request_t, timer);
    uv_timer_stop(timer);

    handle->callbacks.onerror(handle, UV_ETIMEDOUT, handle->user_data);

}

static void uv_http_request_onrecv(uv_conn_t* conn, const struct sockaddr* addr, ssize_t nread, const uv_buf_t* buf,
                                   void* user_data)
{
    http_parser_settings settings;
    uv_http_request_t* handle = container_of(conn, uv_http_request_t, conn);
    uv_timer_stop(&handle->timer);

    if (nread < 0)
    {

        handle->callbacks.onerror(handle, nread, handle->user_data);
    }
    else
    {

        uv_uv_http_request_recv(handle);
        if (nread > 0)
        {

            buf->base[nread] = '\0';

            settings.on_message_begin = http_parse_on_message_begin;
            settings.on_url = null;
            settings.on_status = http_parse_on_status;
            settings.on_header_field = http_parse_on_header_field;
            settings.on_header_value = http_parse_on_header_value;
            settings.on_headers_complete = http_parse_on_header_complete;
            settings.on_body = http_parse_onbody;
            settings.on_message_complete = http_parse_on_message_complete;
            http_parser_execute(&handle->parser, &settings, buf->base, nread);
        }
    }

}

static void uv_uv_http_request_recv(uv_http_request_t* handle)
{
    uv_buf_t recvbuf;

    recvbuf.base = handle->recvbuf;
    recvbuf.len = sizeof(handle->recvbuf) - 1;

    uv_timer_start(&handle->timer, uv_http_request_timeout, UV_HTTP_REQUEST_TIMEOUT, 0);
    uv_conn_recv_some(&handle->conn, &recvbuf, uv_http_request_onrecv, null);

}

static void uv_http_request_onwrite(uv_conn_t* conn, const struct sockaddr* addr, const uv_buf_t bufs[], uint32_t nbufs,
                                    int status, void* user_data)
{
    uv_http_request_t* handle = container_of(conn, uv_http_request_t, conn);
    assert(handle->callbacks.onerror);
    assert(handle->callbacks.onwrite);

    handle->callbacks.onwrite(handle, bufs, nbufs, handle->user_data);

    if (status)
    {

        handle->callbacks.onerror(handle, status, handle->user_data);
    }

}

static void uv_http_request_onsend(uv_conn_t* conn, const struct sockaddr* addr, const uv_buf_t bufs[], uint32_t nbufs,
                                   int status, void* user_data)
{
    uint32_t i = 0;
    uv_http_request_t* handle = container_of(conn, uv_http_request_t, conn);
    uv_timer_stop(&handle->timer);
    sdsfree(bufs[0].base);

    if (status)
    {

        handle->callbacks.onerror(handle, status, handle->user_data);
    }

    http_parser_init(&handle->parser, HTTP_RESPONSE);
    handle->parser.data = handle;

    uv_uv_http_request_recv(handle);

}

static void uv_http_request_send(uv_http_request_t* handle)
{
    QUEUE* q = null;
    uv_http_keyval_t* header = null;
    uv_buf_t buf[2];
    uint32_t count = 0;

    sds buffer = null;

    memset(buf, 0, sizeof(buf));

    buffer = sdsempty();
    assert(buffer);

    buffer = sdscatprintf(buffer, "%s ", http_method_str(handle->method));

    if (handle->uri.path && sdslen(handle->uri.path))
    {

        buffer = sdscatsds(buffer, handle->uri.path);
    }
    else
    {

        buffer = sdscat(buffer, "/");
    }

    if (handle->uri.query && strlen(handle->uri.query))
    {

        buffer = sdscatprintf(buffer, "?%s ", handle->uri.query);
    }

    buffer = sdscat(buffer, " HTTP/1.1\r\n");

    QUEUE_FOREACH(q, &handle->headers_queue)
    {
        header = QUEUE_DATA(q, uv_http_keyval_t, headers_queue);
        buffer = sdscatprintf(buffer, "%s: %s\r\n", header->key, header->value);
    }

    uv_http_header_clean(&handle->headers_queue);

    buffer = sdscat(buffer, "\r\n");

    buf[0].base = buffer;
    buf[0].len = sdslen(buffer);
    count++;

    if (handle->post_data)
    {
        buf[1].base = handle->post_data;
        buf[1].len = handle->post_size;
        count++;
    }

    uv_timer_start(&handle->timer, uv_http_request_timeout, UV_HTTP_REQUEST_TIMEOUT, 0);
    uv_conn_send(&handle->conn, null, buf, count, uv_http_request_onsend, null);

}

static void uv_http_request_on_connect(uv_conn_t* conn, int status, void* user_data)
{
    uv_http_request_t* handle = container_of(conn, uv_http_request_t, conn);
    uv_timer_stop(&handle->timer);

    if (status)
    {

        return handle->callbacks.onerror(handle, status, handle->user_data);
    }

    if (handle->callbacks.onconnect)
    {

        handle->callbacks.onconnect(handle, handle->user_data);
    }

    uv_http_request_send(handle);

}

static void uv_http_request_connect(uv_http_request_t* handle, const char* uri)
{
    int ec = 0;

    ec = uv_http_uri_parser(uri, &handle->uri);
    if (ec)
    {

        handle->callbacks.onerror(handle, ec, handle->user_data);
    }
    else
    {

        if (!handle->uri.scheme)
        {

            handle->callbacks.onerror(handle, UV_EINVAL, handle->user_data);
            return;
        }

        if (!handle->uri.host)
        {

            handle->callbacks.onerror(handle, UV_EINVAL, handle->user_data);
            return;
        }

        uv_http_request_set_head(handle, "Host", handle->uri.host);
        uv_timer_start(&handle->timer, uv_http_request_timeout, UV_HTTP_REQUEST_TIMEOUT, 0);
        uv_conn_connect_to(&handle->conn, handle->uri.host, handle->uri.port, uv_http_request_on_connect, null);
    }

}

void uv_http_request_init(uv_loop_t* loop, uv_http_request_t* handle)
{
    assert(loop && handle);

    memset(handle, 0, sizeof(uv_http_request_t));

    QUEUE_INIT(&handle->headers_queue);

    uv_conn_init(loop, &handle->conn);
    uv_timer_init(loop, &handle->timer);

}

int uv_http_request_close(uv_http_request_t* handle)
{

    int rc = uv_conn_close(&handle->conn);
    if (rc == 0)
    {

        if (handle->post_data)
        {

            free(handle->post_data);
            handle->post_data = NULL;
            handle->post_size = 0;
        }

        uv_close((uv_handle_t*) &handle->timer);
        uv_http_header_clean(&handle->headers_queue);
        uv_http_uri_clean(&handle->uri);
    }

    return rc;
}

void uv_http_request_add_query(uv_http_request_t* handle, const char* query)
{
    assert(handle);

    if (!handle->uri.query)
    {

        handle->uri.query = sdsnew(query);
    }
    else
    {

        handle->uri.query = sdscat(handle->uri.query, query);
    }
}

void uv_http_request_set_callbacks(uv_http_request_t* handle, http_request_callbacks_t* callbacks)
{

    handle->callbacks = (*callbacks);

}

int uv_http_request_set_head(uv_http_request_t* handle, const char* key, const char* value)
{

    return uv_http_header_set(&handle->headers_queue, key, value);
}

void uv_http_request_get(uv_http_request_t* handle, const char* uri, void* user_data)
{
    assert(handle->callbacks.onerror);

    handle->method = HTTP_GET;
    handle->user_data = user_data;

    uv_http_request_connect(handle, uri);

}

void uv_http_request_post(uv_http_request_t* handle, const char* uri, void* user_data)
{
    assert(handle->callbacks.onerror);

    handle->method = HTTP_POST;
    handle->user_data = user_data;

    uv_http_request_connect(handle, uri);

}

void uv_http_request_simple_post(uv_http_request_t* handle, const char* uri, void* post_data, size_t post_size, void* user_data)
{
    assert(handle->callbacks.onerror);

    handle->method = HTTP_POST;
    handle->user_data = user_data;
    if (post_data && post_size)
    {

        handle->post_size = post_size;
        handle->post_data = calloc(1, post_size);
        assert(handle->post_data);
        memcpy(handle->post_data, post_data, post_size);
    }

    uv_http_request_connect(handle, uri);

}

void uv_http_request_write(uv_http_request_t* handle, const uv_buf_t* bufs, uint32_t nbufs)
{
    assert(handle->callbacks.onerror);
    assert(handle->callbacks.onwrite);

    assert(bufs && nbufs);

    uv_conn_send(&handle->conn, null, bufs, nbufs, uv_http_request_onwrite, null);

}

uv_addr_t* uv_http_request_sockname(uv_http_request_t* handle)
{
    assert(handle);

    return uv_conn_sockname(&handle->conn);
}

