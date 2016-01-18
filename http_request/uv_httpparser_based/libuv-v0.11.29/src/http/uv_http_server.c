#include <unistd.h>
#include "uv.h"
#include "mime.h"

#if 0
#  define HTTP_DEBUG_PRINTF(str...) \
      do { \
          fprintf(stderr, "---->%u:%s ", __LINE__, __FUNCTION__); \
          fprintf(stderr, str); \
      } while(0)
#else
#  define HTTP_DEBUG_PRINTF(str...)
#endif
#define UV_HTTP_SERVER_KEEPALIVE_TIMEOUT 3000
static void uv_http_server_onsendfile(uv_stream_t* stream, int in_fd, off_t offset, size_t count, int status, void* user_data);
static void uv_http_server_req_send_headers(uv_http_server_req_t* req);
static void uv_http_server_req_close(uv_http_server_req_t* req);
static void uv_http_server_req_end(uv_http_server_req_t* req);

/******************route********************/

//static void uv_http_server_route_print(uv_http_server_t* server)
//{
//    QUEUE* q = null;
//    uv_http_server_route_t* route = null;
//
//    QUEUE_FOREACH(q, &server->routes_queue)
//    {
//        route = QUEUE_DATA(q, uv_http_server_route_t, routes_queue);
//        printf("---->%s:%u    [%p][%p][%p].\n", __FILE__, __LINE__, server, route, q);
//    }
//}
static uv_http_server_route_t* uv_http_server_route_get(uv_http_server_t* server, enum http_method mothod, const char* path)
{
    QUEUE* q = null;
    uv_http_server_route_t* route = null;
//    uv_http_server_route_print(server);

    QUEUE_FOREACH(q, &server->routes_queue)
    {
        route = QUEUE_DATA(q, uv_http_server_route_t, routes_queue);
//        printf("---->%s:%u    [%p][%p][%p] [%s]-[%s] [%d]-[%d].\n", __FILE__, __LINE__, server, route, q, path, route->path,
//               mothod, route->method);
        if (strcasecmp(path, route->path) == 0 && mothod == route->method)
        {
            return route;
        }
    }

    return null;
}

uv_http_server_route_t* uv_http_server_route_add(uv_http_server_t* server, enum http_method method, const char* pathname)
{
    uv_http_server_route_t* route = null;
//    uv_http_server_route_print(server);

    route = uv_http_server_route_get(server, method, pathname);
    if (!route)
    {
        route = calloc(1, sizeof(uv_http_server_route_t));
        ASSERT(route);
        route->path = sdsnew(pathname);
        ASSERT(route->path);
        route->method = method;
        QUEUE_INSERT_TAIL(&server->routes_queue, &route->routes_queue);
//        uv_http_server_route_print(server);
    }
    return route;
}

/*******************static file*************/

static int uv_fs_isdir(const char* pathname)
{
    struct stat buf;
    int cc = stat(pathname, &buf);
    if (!cc && S_ISDIR(buf.st_mode))
    {
        return 0;
    }
    return cc | -1;
}

static size_t uv_fs_fsize(const char* pathname)
{
    size_t size = 0;
    struct stat buf;
    int cc = stat(pathname, &buf);
    if (!cc && (S_ISREG(buf.st_mode) || S_ISLNK(buf.st_mode)) && (buf.st_mode & S_IRUSR))
    {
        size = buf.st_size;
    }
    return size;
}

static int uv_http_server_static(uv_http_server_req_t *req)
{
    char need_charset = 0;
    int fd = -1;
    sds pathname = null;
    char content_length[64] = "";
    size_t size = 0;

    pathname = sdsnew(req->server->wwwroot);
    pathname = sdscatsds(pathname, req->url.path);

    if (uv_fs_isdir(pathname) == 0)
    {
        pathname = sdscat(pathname, "/index.html");
    }

    if ((size = uv_fs_fsize(pathname)) == 0)
    {
        HTTP_DEBUG_PRINTF("%s file size is 0.\n", pathname);
        sdsfree(pathname);
        return -1;
    }

    if ((fd = open(pathname, O_RDONLY)) < 0)
    {
        HTTP_DEBUG_PRINTF("%s open failed.\n", pathname);
        sdsfree(pathname);
        return -1;
    }

    uv_http_server_set_status(req, UV_HTTP_OK);
    uv_http_server_req_set_head(req, "Content-type", mime_get_path(pathname, &need_charset));
    uv_http_server_req_set_head(req, "Server", "HM-WS");
    uv_http_server_req_set_head(req, "Cache-Control", "private");

    if (req->keepalive)
    {
        uv_http_server_req_set_head(req, "Connection", "Keep-Alive");
    }
    else
    {
        uv_http_server_req_set_head(req, "Connection", "Close");
    }

    snprintf(content_length, sizeof(content_length), "%zd", size);
    uv_http_server_req_set_head(req, "Content-Length", content_length);

    uv_http_server_req_send_headers(req);

    HTTP_DEBUG_PRINTF("%s sendfile size - %zd.\n", pathname, size);
    uv_sendfile((uv_stream_t*) &req->handle, fd, 0, size, uv_http_server_onsendfile, null);

    sdsfree(pathname);
    return 0;
}

/******************http_parser********************/
static int uv_http_server_on_url(http_parser *parser, const char *at, size_t len)
{
    uv_http_server_req_t* req = (uv_http_server_req_t*) parser->data;

    if (req->url.full)
    {
        req->url.full = sdscatlen((sds) req->url.full, at, len);
    }
    else
    {
        req->url.full = sdsnewlen(at, len);
    }

    return 0;
}

static int uv_http_server_on_header_field(http_parser* parser, const char* at, size_t len)
{
    uv_http_server_req_t* req = (uv_http_server_req_t*) parser->data;

    if (!req->kv)
    {
        req->kv = calloc(1, sizeof(uv_http_keyval_t));
        ASSERT(req->kv);
    }

    if (!req->kv->key)
    {
        req->kv->key = sdsnewlen(at, len);
    }
    else
    {
        if (!req->kv->value)
        {
            req->kv->key = sdscatlen(req->kv->key, at, len);
        }
        else
        {
            uv_http_header_add(&req->iheaders_queue, req->kv);
            req->kv = null;
            return uv_http_server_on_header_field(parser, at, len);
        }
    }

    return 0;
}

static int uv_http_server_on_header_value(http_parser *parser, const char *at, size_t len)
{
    uv_http_server_req_t *req = (uv_http_server_req_t*) parser->data;
    ASSERT(req->kv);

    if (!req->kv->value)
    {
        req->kv->value = sdsnewlen(at, len);
    }
    else
    {
        req->kv->value = sdscatlen(req->kv->value, at, len);
    }

    return 0;
}

static int uv_http_server_on_headers_complete(http_parser *parser)
{
    uv_http_server_req_t* req = (uv_http_server_req_t*) parser->data;
    struct http_parser_url url;

    req->method = parser->method;

    http_parser_parse_url(req->url.full, sdslen(req->url.full), 1, &url);

#define UF_OFFSET(X) url.field_data[X].off
#define UF_LEN(X) url.field_data[X].len
#define UF_SET(X) (url.field_set & (1 << (X)))
#define UF_CHECK_AND_SET(X, DST) \
    if (UF_SET(X)) \
        (DST) = sdsnewlen(req->url.full + UF_OFFSET(X), UF_LEN(X))

    UF_CHECK_AND_SET(UF_SCHEMA, req->url.schema);
    UF_CHECK_AND_SET(UF_HOST, req->url.host);
    UF_CHECK_AND_SET(UF_PORT, req->url.port);
    UF_CHECK_AND_SET(UF_PATH, req->url.path);
    UF_CHECK_AND_SET(UF_QUERY, req->url.query);
    UF_CHECK_AND_SET(UF_FRAGMENT, req->url.fragment);
    UF_CHECK_AND_SET(UF_USERINFO, req->url.userinfo);

#undef UF_CHECK_AND_SET
#undef UF_SET
#undef UF_LEN
#undef UF_OFFSET

    if (req->kv)
    {
        uv_http_header_add(&req->iheaders_queue, req->kv);
        req->kv = null;
    }

    HTTP_DEBUG_PRINTF("%p - path: %s.\n", req, req->url.full);

    if (parser->upgrade)
    {
        return 0;
    }

    req->route = uv_http_server_route_get(req->server, req->method, req->url.path);
    if (req->route && req->route->onheaders)
    {
        req->route->onheaders(req);
    }
	if(req->server->user_authentication){
		req->server->user_authentication(req);
	}
    return 0;
}

static int uv_http_server_on_body(http_parser *parser, const char *at, size_t len)
{
    uv_http_server_req_t* req = (uv_http_server_req_t*) parser->data;

    if (req->route && req->route->onbody)
    {
        req->route->onbody(req, at, len);
    }
    else
    {
        if (req->content)
        {
            req->content = sdscatlen((sds) req->content, at, len);
        }
        else
        {
            req->content = sdsnewlen(at, len);
        }
    }
    return 0;
}

static int uv_http_server_on_message_complete(http_parser *parser)
{
    uv_http_server_req_t* req = (uv_http_server_req_t*) parser->data;

    req->keepalive = http_should_keep_alive(parser);

    req->http_major = parser->http_major;
    req->http_minor = parser->http_minor;

    if (req->route)
    {
        ASSERT(req->route->oncomplete);
        uv_http_server_req_set_head(req, "Server", "HM-WS");
        uv_http_server_req_set_head(req, "Cache-Control", "no-cache");

        if (req->keepalive)
        {
            uv_http_server_req_set_head(req, "Connection", "Keep-Alive");
        }
        else
        {
            uv_http_server_req_set_head(req, "Connection", "Close");
        }

        if (req->route->oncomplete(req) == 0)
        {
            return 0;
        }
    }

    if (parser->upgrade)
    {
        return 0;
    }

    if (uv_http_server_static(req) == 0)
    {
        return 0;
    }

    uv_http_server_req_error(req, UV_HTTP_NOT_FOUND);

    return 0;
}

/***************request*******************/

static void uv_http_server_req_onwrite_headers(uv_stream_t* stream, int status, const uv_buf_t bufs[], unsigned int nbufs,
                                               void* user_data)
{
    int i = 0;
    uv_http_server_req_t* req = container_of(stream, uv_http_server_req_t, handle);

    for (i = 0; i < nbufs; i++)
    {
        sdsfree(bufs[i].base);
    }

    if (status)
    {
        HTTP_DEBUG_PRINTF("uv http write headers error: %s.\n", uv_strerror(status));
        uv_http_server_req_close(req);
    }
}

static void uv_http_server_req_onwrite_404(uv_stream_t* stream, int status, const uv_buf_t bufs[], unsigned int nbufs,
                                           void* user_data)
{
    int i = 0;
    uv_http_server_req_t* req = container_of(stream, uv_http_server_req_t, handle);

    for (i = 0; i < nbufs; i++)
    {
        sdsfree(bufs[i].base);
    }

    if (status)
    {
        HTTP_DEBUG_PRINTF("uv http write 404 error: %s.\n", uv_strerror(status));
        uv_http_server_req_close(req);
    }
    else
    {
        uv_http_server_req_end(req);
    }
}

static void uv_http_server_req_onwrite_content(uv_stream_t* stream, int status, const uv_buf_t bufs[], unsigned int nbufs,
                                               void* user_data)
{
    int i = 0;
    uv_http_cgi_delay_free content_free = user_data;
    uv_http_server_req_t* req = container_of(stream, uv_http_server_req_t, handle);

    for (i = 0; i < nbufs; i++)
    {
        if (content_free)
        {
            content_free(bufs[i].base);
        }
    }

    if (status)
    {
        HTTP_DEBUG_PRINTF("uv http write content error: %s.\n", uv_strerror(status));
        uv_http_server_req_close(req);
    }
    else
    {
        uv_http_server_req_end(req);
    }
}

static void uv_http_server_onsendfile(uv_stream_t* stream, int in_fd, off_t offset, size_t count, int status, void* user_data)
{
    uv_http_server_req_t* req = container_of(stream, uv_http_server_req_t, handle);

    close(in_fd);

    if (status)
    {
        HTTP_DEBUG_PRINTF("uv http sendfile error: %s.\n", uv_strerror(status));
        uv_http_server_req_close(req);
    }
    else
    {
        uv_http_server_req_end(req);
    }
}

static void uv_http_server_req_send_headers(uv_http_server_req_t* req)
{
    uv_buf_t bufs;
    QUEUE* q = null;
    uv_http_keyval_t* kv = null;
    sds s = sdsempty();

    s = sdscatprintf(s, "HTTP/1.1 %d %s\r\n", req->send_status, uv_http_status_code_str(req->send_status));
    QUEUE_FOREACH(q, &req->oheaders_queue)
    {
        kv = QUEUE_DATA(q, uv_http_keyval_t, headers_queue);
        s = sdscatprintf(s, "%s: %s\r\n", kv->key, kv->value);
    }

    s = sdscat(s, "\r\n");

    bufs.base = s;
    bufs.len = sdslen(s);

    uv_write((uv_stream_t*) &req->handle, &bufs, 1, uv_http_server_req_onwrite_headers, null);
}

void uv_http_server_set_status(uv_http_server_req_t* req, int status)
{
    req->send_status = status;
}

void uv_http_server_req_error(uv_http_server_req_t* req, int error)
{
    sds s404_buf = sdsempty();
    uv_buf_t bufs;
    char content_length[64] = "";

    HTTP_DEBUG_PRINTF("http-error: %d %s.\n", error, uv_http_status_code_str(error));

    uv_http_server_set_status(req, error);

    uv_http_server_req_set_head(req, "Content-type", "text/html");
    uv_http_server_req_set_head(req, "Server", "HM-WS");
    uv_http_server_req_set_head(req, "Cache-Control", "private");

    if (req->keepalive)
    {
        uv_http_server_req_set_head(req, "Connection", "Keep-Alive");
    }
    else
    {
        uv_http_server_req_set_head(req, "Connection", "Close");
    }

    s404_buf = sdscatprintf(s404_buf, "<html>"
                            "<head><title>404 Not Found</title></head>"
                            "<body>"
                            "<center><h1>404 Not Found</h1></center>"
                            "<hr>"
                            "<center>url:%s</center>"
                            "<center>HM-HTTP-SERVER/1.0.0</center>"
                            "</body>"
                            "</html>",
                            req->url.full);
    snprintf(content_length, sizeof(content_length), "%zd", sdslen(s404_buf));
    uv_http_server_req_set_head(req, "Content-Length", content_length);
    uv_http_server_req_send_headers(req);
    bufs.base = s404_buf;
    bufs.len = sdslen(s404_buf);
    uv_write((uv_stream_t*) &req->handle, &bufs, 1, uv_http_server_req_onwrite_404, null);
}

static void uv_http_server_req_reset(uv_http_server_req_t* req)
{
    req->keepalive = 0;

    if (req->route && req->route->ondisconnect)
    {
        req->route->ondisconnect(req);
        req->route = null;
    }

    uv_http_header_clean(&req->iheaders_queue);
    uv_http_header_clean(&req->oheaders_queue);

#define HTTP_SDSRESET(s) \
    if (s) \
    { \
        sdsfree(s); \
        s = null; \
    }

    HTTP_SDSRESET(req->content)
    HTTP_SDSRESET(req->url.full);
    HTTP_SDSRESET(req->url.schema);
    HTTP_SDSRESET(req->url.host);
    HTTP_SDSRESET(req->url.port);
    HTTP_SDSRESET(req->url.path);
    HTTP_SDSRESET(req->url.query);
    HTTP_SDSRESET(req->url.fragment);
    HTTP_SDSRESET(req->url.userinfo);

#undef HTTP_SDSRESET
}

static void uv_http_server_req_close(uv_http_server_req_t* req)
{
    if (uv_close((uv_handle_t*) &req->handle) == 0)
    {
        if (req->timeron)
        {
            req->timeron = 0;
            uv_timer_stop(&req->timer);
            uv_close((uv_handle_t*) &req->timer);
        }
        uv_http_server_req_reset(req);
        if (!QUEUE_EMPTY(&req->clients_queue))
        {
            QUEUE_REMOVE(&req->clients_queue);
        }
        HTTP_DEBUG_PRINTF("%p - close.\n", req);
        free(req);
    }
}

static void uv_http_server_req_ontimer(uv_timer_t* handle)
{
    uv_http_server_req_t* req = container_of(handle, uv_http_server_req_t, timer);
    uv_http_server_req_close(req);
}

static void uv_http_server_req_end(uv_http_server_req_t* req)
{
    if (req->keepalive)
    {
        req->timeron = 1;
        uv_http_server_req_reset(req);
        uv_timer_init(req->handle.loop, &req->timer);
        uv_timer_start(&req->timer, uv_http_server_req_ontimer, UV_HTTP_SERVER_KEEPALIVE_TIMEOUT, 0);
    }
    else
    {
        uv_http_server_req_close(req);
    }
}

const char* uv_http_server_get_head(uv_http_server_req_t *req, const char *key)
{
    uv_http_keyval_t* kv;
    kv = uv_http_header_find(&req->iheaders_queue, key);
    if (kv != NULL)
    {
        return kv->value;
    }
    return NULL;
}

int uv_http_server_req_set_head(uv_http_server_req_t* req, const char* key, const char* val)
{
    return uv_http_header_set(&req->oheaders_queue, key, val);
}

void uv_http_server_req_send(uv_http_server_req_t* req, char* content, size_t len, uv_http_cgi_delay_free content_free)
{
    ASSERT(req);
    uv_buf_t bufs;
    char content_length[64] = "";
    snprintf(content_length, sizeof(content_length), "%zd", len);
    uv_http_server_req_set_head(req, "Content-Length", content_length);
    uv_http_server_req_send_headers(req);
    bufs.base = content;
    bufs.len = len;
    uv_write((uv_stream_t*) &req->handle, &bufs, 1, uv_http_server_req_onwrite_content, content_free);
}

void uv_http_server_req_ws_response(uv_http_server_req_t* req, int error, sds wskey)
{
    HTTP_DEBUG_PRINTF("http-error: %d %s.\n", error, uv_http_status_code_str(error));

    uv_http_server_set_status(req, error);

    uv_http_server_req_set_head(req, "Server", "HM-WS");
    uv_http_server_req_set_head(req, "Cache-Control", "private");

    if (wskey)
    {
        uv_http_server_req_set_head(req, "Connection", "Upgrade");
        uv_http_server_req_set_head(req, "Upgrade", "WebSocket");
        uv_http_server_req_set_head(req, "Access-Control-Allow-Credentials", "true");
        uv_http_server_req_set_head(req, "Access-Control-Allow-Headers", "content-type");
        uv_http_server_req_set_head(req, "Sec-WebSocket-Accept", wskey);
    }

    uv_http_server_req_send_headers(req);
}

const char *BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void base64_encode_triple(const char triple[3], char result[4])
{
    int tripleValue, i;

    tripleValue = triple[0];
    tripleValue *= 256;
    tripleValue += triple[1];
    tripleValue *= 256;
    tripleValue += triple[2];

    for (i = 0; i < 4; i++)
    {
        result[3 - i] = BASE64[tripleValue % 64];
        tripleValue /= 64;
    }
}
//int base64_encode(const char* input, const int input_size, char* output, const int output_size)
int base64_encode(const char* input, const int input_size, char* output, const int output_size)
{
    int source_len = input_size;

    /* check if the result will fit in the target buffer */
    if ((source_len + 2) / 3 * 4 > output_size - 1)
    {
        return 0;
    }

    /* encode all full triples */
    while (source_len >= 3)
    {
        base64_encode_triple(input, output);
        source_len -= 3;
        input += 3;
        output += 4;
    }

    /* encode the last one or two characters */
    if (source_len > 0)
    {
        char temp[3];
        memset(temp, 0, sizeof(temp));
        memcpy(temp, input, source_len);
        base64_encode_triple(temp, output);
        output[3] = '=';
        if (source_len == 1)
        {
            output[2] = '=';
        }

        output += 4;
    }

    /* terminate the string */
    output[0] = 0;

    return 1;
}

static void uv_http_server_req_ws(uv_http_server_req_t* req)
{
#if 0
    QUEUE* q = null;
    uv_http_keyval_t* kv = null;
    sds wskey = null;
    SHA1_CTX ctx;
    const char* wskey_xx = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char digest[20] = "";
    char hash[41] = "";
    char hash64[129] = "";

    QUEUE_FOREACH(q, &req->iheaders_queue)
    {
        kv = QUEUE_DATA(q, uv_http_keyval_t, headers_queue);
        if (strcmp(kv->key, "Sec-WebSocket-Key") == 0)
        {
            wskey = kv->value;
            printf("request key: %s\n", wskey);
        }
    }

    if (wskey)
    {
        SHA1Init(&ctx);
        SHA1Update(&ctx, (const unsigned char*) wskey, sdslen(wskey));
        SHA1Update(&ctx, (const unsigned char*) wskey_xx, strlen(wskey_xx));
        SHA1Final(digest, &ctx);
//        SHA1Hash(digest, 20, hash, 40);
        printf("response key: %s\n", hash);
        base64_encode((char*) digest, 20, hash64, 128);
        printf("response key64: %s\n", hash64);
        uv_http_server_req_ws_response(req, UV_HTTP_SWITCHING_PROTOCOLS, hash64);
    }
    else
    {
        uv_http_server_req_ws_response(req, UV_HTTP_BAD_REQUEST, null);
    }
#endif
}

void uv_http_server_req_ws_data(uv_http_server_req_t* req, char* data, size_t len)
{
    printf("ws data-len: %zd\n", len);
    printf("ws data: %s\n", &data[14]);
}

void uv_http_server_req_read_cancel(uv_http_server_req_t* req)
{
    uv_read_cancel((uv_stream_t*) &req->handle);
}

/****************server******************/
static void uv_http_server_req_read(uv_http_server_req_t* req);
static void uv_http_server_req_onread(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf, void* user_data)
{
    size_t nparsed = 0;
    uv_http_server_req_t* req = container_of(stream, uv_http_server_req_t, handle);

    if (nread < 0)
    {
        uv_http_server_req_close(req);
    }
    else
    {
        uv_http_server_req_read(req);
        if (nread > 0)
        {
            buf->base[nread] = '\0';
            if (req->timeron)
            {
                req->timeron = 0;
                uv_timer_stop(&req->timer);
                uv_close((uv_handle_t*) &req->timer);
            }
//        printf("%s", buf->base);
            if (!req->parser.upgrade)
            {
                nparsed = http_parser_execute(&req->parser, &req->parser_settings, buf->base, nread);
                if (req->parser.upgrade)
                {
                    uv_http_server_req_ws(req);

                    if (nparsed != nread)
                    {
                        uv_http_server_req_ws_data(req, &buf->base[nparsed], nread - nparsed);
                    }
                }
                else if (nparsed != nread)
                {
                    uv_http_server_req_close(req);
                }
            }
            else
            {
                uv_http_server_req_ws_data(req, buf->base, nread);
            }
        }
    }
}

static void uv_http_server_req_read(uv_http_server_req_t* req)
{
    uv_buf_t rbuf;
    rbuf.base = req->rbuffers;
    rbuf.len = sizeof(req->rbuffers) - 1;
    uv_read_some((uv_stream_t *) &req->handle, &rbuf, uv_http_server_req_onread, null);
}

static void uv_http_server_accept(uv_stream_t* listener, int status, void* user_data)
{

    uv_http_server_t* server = container_of(listener, uv_http_server_t, listener);
    uv_http_server_req_t* req = calloc(1, sizeof(uv_http_server_req_t));
    ASSERT(req);

    QUEUE_INIT(&req->clients_queue);
    QUEUE_INIT(&req->iheaders_queue);
    QUEUE_INIT(&req->oheaders_queue);

    uv_tcp_init(listener->loop, &req->handle);

    if (uv_accept(listener, (uv_stream_t*) &req->handle))
    {
        free(req);
        return;
    }

    HTTP_DEBUG_PRINTF("%p - accept.\n", req);

    uv_tcp_nodelay(&req->handle, 1);

    req->server = server;
    http_parser_init(&req->parser, HTTP_REQUEST);
    req->parser.data = req;

    QUEUE_INSERT_TAIL(&server->clients_queue, &req->clients_queue);

    /*default parser_settings, cgi maybe replace this settings*/
    req->parser_settings.on_message_begin = null;
    req->parser_settings.on_url = uv_http_server_on_url;
    req->parser_settings.on_status = null;
    req->parser_settings.on_header_field = uv_http_server_on_header_field;
    req->parser_settings.on_header_value = uv_http_server_on_header_value;
    req->parser_settings.on_headers_complete = uv_http_server_on_headers_complete;
    req->parser_settings.on_body = uv_http_server_on_body;
    req->parser_settings.on_message_complete = uv_http_server_on_message_complete;

    uv_http_server_req_read(req);
}

int uv_http_server_init(uv_loop_t *loop, uv_http_server_t *server)
{
    memset(server, 0, sizeof(uv_http_server_t));
    uv_tcp_init(loop, &server->listener);
    QUEUE_INIT(&server->clients_queue);
    QUEUE_INIT(&server->routes_queue);
    return 0;
}

int uv_http_server_start(uv_http_server_t* server, int port, const char* wwwroot)
{
    uv_addr_t addr;
    if (wwwroot)
    {
        server->wwwroot = strdup(wwwroot);
        ASSERT(server->wwwroot);
    }

    uv_ip_addr("0.0.0.0", port, &addr);
    uv_tcp_bind(&server->listener, &addr.addr, 0);
    uv_listen((uv_stream_t *) &server->listener, 10, uv_http_server_accept, null);

    return 0;
}

int uv_http_server_close(uv_http_server_t* server)
{
    QUEUE* q = null;
    uv_http_server_req_t* req = null;
    uv_http_server_route_t* route = null;

    if (server->wwwroot)
    {
        free(server->wwwroot);
        server->wwwroot = null;
    }

    while (!QUEUE_EMPTY(&server->clients_queue))
    {
        q = QUEUE_HEAD(&server->clients_queue);
        QUEUE_REMOVE(q);
        QUEUE_INIT(q);
        req = QUEUE_DATA(q, uv_http_server_req_t, clients_queue);
        uv_http_server_req_close(req);
    }

    while (!QUEUE_EMPTY(&server->routes_queue))
    {
        q = QUEUE_HEAD(&server->routes_queue);
        QUEUE_REMOVE(q);
        QUEUE_INIT(q);
        route = QUEUE_DATA(q, uv_http_server_route_t, routes_queue);
        if (route->path)
        {
            sdsfree(route->path);
            route->path = null;
        }
        free(route);
    }

    ASSERT(uv_close((uv_handle_t * ) &server->listener) == 0);

    return 0;
}

