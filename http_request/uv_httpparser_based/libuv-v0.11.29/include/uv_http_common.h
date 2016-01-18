/*
 * uv_http_common.h
 *
 *  Created on: 2014-8-1
 *      Author: wang.guilin
 */

#ifndef UV_HTTP_COMMON_H_
#define UV_HTTP_COMMON_H_

#include "queue.h"
#include "sds.h"

#define UV_HTTP_STATUS_CODE_MAP(XX) \
    XX(100, CONTINUE, "Continue") \
    XX(101, SWITCHING_PROTOCOLS, "Switching Protocols") \
    XX(200, OK, "OK") \
    XX(201, CREATED, "Created") \
    XX(202, ACCEPTED, "Accepted") \
    XX(203, NON_AUTHORITATIVE_INFORMATION, "Non-Authoritative Information") \
    XX(204, NO_CONTENT, "No Content") \
    XX(205, RESET_CONTENT, "Reset Content") \
    XX(206, PARTIAL_CONTENT, "Partial Content") \
    XX(300, MULTIPLE_CHOICES, "Multiple Choices") \
    XX(301, MOVED_PERMANENTLY, "Moved Permanently") \
    XX(302, FOUND, "Found") \
    XX(303, SEE_OTHER, "See Other") \
    XX(304, NOT_MODIFIED, "Not Modified") \
    XX(305, USE_PROXY, "Use Proxy") \
    XX(307, TEMPORARY_REDIRECT, "Temporary Redirect") \
    XX(400, BAD_REQUEST, "Bad Request") \
    XX(401, UNAUTHORIZED, "Unauthorized") \
    XX(402, PAYMENT_REQUIRED, "Payment Required") \
    XX(403, FORBIDDEN, "Forbidden") \
    XX(404, NOT_FOUND, "Not Found") \
    XX(405, METHOD_NOT_ALLOWED, "Method Not Allowed") \
    XX(406, NOT_ACCEPTABLE, "Not Acceptable") \
    XX(407, PROXY_AUTHENTICATION_REQUIRED, "Proxy Authentication Required") \
    XX(408, REQUEST_TIMEOUT, "Request Timeout") \
    XX(409, CONFLICT, "Conflict") \
    XX(410, GONE, "Gone") \
    XX(411, LENGTH_REQUIRED, "Length Required") \
    XX(412, PRECONDITION_FAILED, "Precondition Failed") \
    XX(413, REQUEST_ENTITY_TOO_LARGE, "Request Entity Too Large") \
    XX(414, REQUEST_URI_TOO_LONG, "Request-URI Too Long") \
    XX(415, UNSUPPORTED_MEDIA_TYPE, "Unsupported Media Type") \
    XX(416, REQUESTED_RANGE_NOT_SATISFIABLE, "Requested Range Not Satisfiable") \
    XX(417, EXPECTATION_FAILED, "Expectation Failed") \
    XX(418, IM_A_TEAPOT, "I'm a teapot") /* ;-) */ \
    XX(500, INTERNAL_SERVER_ERROR, "Internal Server Error") \
    XX(501, NOT_IMPLEMENTED, "Not Implemented") \
    XX(502, BAD_GATEWAY, "Bad Gateway") \
    XX(503, SERVICE_UNAVAILABLE, "Service Unavailable") \
    XX(504, GATEWAY_TIMEOUT, "Gateway Timeout") \
    XX(505, HTTP_VERSION_NOT_SUPPORTED, "HTTP Version Not Supported")

enum
{
#define XX(CODE, NAME, STR) UV_HTTP_ ## NAME = CODE,
    UV_HTTP_STATUS_CODE_MAP(XX)
#undef XX
};

typedef struct uv_http_keyval_s
{
    sds key;
    sds value;
    QUEUE headers_queue;
} uv_http_keyval_t;

typedef struct uv_http_uri_s
{
//    unsigned flags;
    sds scheme; /* scheme; e.g http, ftp etc */
    sds userinfo; /* userinfo (typically username:pass), or null */
    sds host; /* hostname, IP address, or null */
    sds path; /* path, or "". */
    sds query; /* query, or null */
    sds fragment; /* fragment or null */
    int port; /* port, or zero */
} uv_http_uri_t;

int uv_http_header_add(QUEUE* headers_queue, uv_http_keyval_t* kv);
int uv_http_header_set(QUEUE* headers_queue, const char* key, const char* value);
void uv_http_header_clean(QUEUE* headers_queue);
int uv_http_uri_parser(const char* uri, uv_http_uri_t* http_uri);
void uv_http_uri_clean(uv_http_uri_t* http_uri);
const char* uv_http_status_code_str(int status);
 uv_http_keyval_t* uv_http_header_find(QUEUE* headers_queue, const char* key);

#endif /* UV_HTTP_COMMON_H_ */
