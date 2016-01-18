/*
 * uv_http_common.c
 *
 *  Created on: 2014-8-1
 *      Author: wang.guilin
 */

#include <stdlib.h>
#include <string.h>
#include "uv.h"
#include "uv_http_common.h"

static int uv_http_header_add_internal(QUEUE* headers_queue, const char *key, const char *value)
{
    uv_http_keyval_t* header = calloc(1, sizeof(uv_http_keyval_t));
    if (!header)
    {
        return UV_ENOMEM;
    }

    header->key = sdsnew(key);
    header->value = sdsnew(value);

    if (!header->key || !header->value)
    {
        if (header->key)
        {
            sdsfree(header->key);
            header->key = null;
        }

        if (header->value)
        {
            sdsfree(header->value);
            header->value = null;
        }
        return UV_ENOMEM;
    }

    QUEUE_INSERT_TAIL(headers_queue, &header->headers_queue);

    return 0;
}

static void uv_http_header_free(uv_http_keyval_t* header)
{
    ASSERT(header);

    if (header->key)
    {
        sdsfree(header->key);
        header->key = null;
    }

    if (header->value)
    {
        sdsfree(header->value);
        header->value = null;
    }
    free(header);
}

uv_http_keyval_t* uv_http_header_find(QUEUE* headers_queue, const char* key)
{
    QUEUE* q = null;
    uv_http_keyval_t* header = null;
    QUEUE_FOREACH(q, headers_queue)
    {
        header = QUEUE_DATA(q, uv_http_keyval_t, headers_queue);
        if (strcasecmp(header->key, key) == 0)
        {
            return header;
        }
    }
    return null;
}

int uv_http_header_add(QUEUE* headers_queue, uv_http_keyval_t* kv)
{
    QUEUE_INSERT_TAIL(headers_queue, &kv->headers_queue);
    return 0;
}

int uv_http_header_set(QUEUE* headers_queue, const char* key, const char* value)
{
    uv_http_keyval_t* header = null;

    if (strchr(key, '\r') || strchr(key, '\n'))
    {
        /* drop illegal headers */
        return UV_EINVAL;
    }

    if (strchr(value, '\r') || strchr(value, '\n'))
    {
        /* drop illegal headers */
        return UV_EINVAL;
    }

    header = uv_http_header_find(headers_queue, key);
    if (header)
    {
        if (header->value)
        {
            sdsfree(header->value);
            header->value = null;
        }

        header->value = sdsnew(value);

        if (header->value)
        {
            return 0;
        }
        else
        {
            return UV_ENOMEM;
        }
    }
    else
    {
        return uv_http_header_add_internal(headers_queue, key, value);
    }
}

void uv_http_header_clean(QUEUE* headers_queue)
{
    QUEUE* q = null;
    uv_http_keyval_t* header = null;
    while (!QUEUE_EMPTY(headers_queue))
    {
        q = QUEUE_HEAD(headers_queue);
        QUEUE_REMOVE(q);
        QUEUE_INIT(q);
        header = QUEUE_DATA(q, uv_http_keyval_t, headers_queue);
        uv_http_header_free(header);
    }
}

#define SUBDELIMS "!$&'()*+,;="

static const char uri_chars[256] = {
    /* 0 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 0, 0, 0, 0, 0, 0,
    /* 64 */
    0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 1, 0,
    /* 128 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    /* 192 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};

#define CHAR_IS_UNRESERVED(c) (uri_chars[(unsigned char)(c)])

static int scheme_ok(const char *s, const char *eos)
{
    /* scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) */
    ASSERT(eos >= s);
    if (s == eos)
    {
        return 0;
    }
    if (!ISALPHA(*s))
    {
        return 0;
    }
    while (++s < eos)
    {
        if (!ISALNUM(*s) && *s != '+' && *s != '-' && *s != '.')
        {
            return 0;
        }
    }
    return 1;
}

static char* end_of_authority(char *cp)
{
    while (*cp)
    {
        if (*cp == '?' || *cp == '#' || *cp == '/')
        {
            return cp;
        }
        ++cp;
    }
    return cp;
}

static int userinfo_ok(const char *s, const char *eos)
{
    while (s < eos)
    {
        if (CHAR_IS_UNRESERVED(*s) || strchr(SUBDELIMS, *s) || *s == ':')
        {
            ++s;
        }
        else if (*s == '%' && s + 2 < eos && ISXDIGIT(s[1]) && ISXDIGIT(s[2]))
        {
            s += 3;
        }
        else
        {
            return 0;
        }
    }
    return 1;
}

static int parse_port(const char *s, const char *eos)
{
    int portnum = 0;
    while (s < eos)
    {
        if (!ISDIGIT(*s))
        {
            return -1;
        }
        portnum = (portnum * 10) + (*s - '0');
        if (portnum < 0)
        {
            return -1;
        }
        ++s;
    }
    return portnum;
}

/* returns 0 for bad, 1 for ipv6, 2 for IPvFuture */
static int bracket_addr_ok(const char *s, const char *eos)
{
    if (s + 3 > eos || *s != '[' || *(eos - 1) != ']')
    {
        return 0;
    }
    if (s[1] == 'v')
    {
        /* IPvFuture, or junk.
         "v" 1*HEXDIG "." 1*( unreserved / sub-delims / ":" )
         */
        s += 2; /* skip [v */
        --eos;
        if (!ISXDIGIT(*s)) /*require at least one*/
        {
            return 0;
        }
        while (s < eos && *s != '.')
        {
            if (ISXDIGIT(*s))
            {
                ++s;
            }
            else
            {
                return 0;
            }
        }
        if (*s != '.')
        {
            return 0;
        }
        ++s;
        while (s < eos)
        {
            if (CHAR_IS_UNRESERVED(*s) || strchr(SUBDELIMS, *s) || *s == ':')
            {
                ++s;
            }
            else
            {
                return 0;
            }
        }
        return 2;
    }
    else
    {
        /* IPv6, or junk */
        char buf[64];
        ssize_t n_chars = eos - s - 2;
        struct in6_addr in6;
        if (n_chars >= 64) /* way too long */
        {
            return 0;
        }
        memcpy(buf, s + 1, n_chars);
        buf[n_chars] = '\0';
        return (uv_inet_pton(AF_INET6, buf, &in6) == 1) ? 1 : 0;
    }
    return 0;
}

static int regname_ok(const char *s, const char *eos)
{
    while (s && s < eos)
    {
        if (CHAR_IS_UNRESERVED(*s) || strchr(SUBDELIMS, *s))
        {
            ++s;
        }
        else if (*s == '%' && ISXDIGIT(s[1]) && ISXDIGIT(s[2]))
        {
            s += 3;
        }
        else
        {
            return 0;
        }
    }
    return 1;
}

static int parse_authority(uv_http_uri_t* uri, char *s, char *eos)
{
    char *cp, *port;
    ASSERT(eos);
    if (eos == s)
    {
        uri->host = sdsnew("");
        if (uri->host == null)
        {
            return UV_ENOMEM;
        }
        return 0;
    }

    /* Optionally, we start with "userinfo@" */

    cp = strchr(s, '@');
    if (cp && cp < eos)
    {
        if (!userinfo_ok(s, cp))
        {
            return UV_EINVAL;
        }
        *cp++ = '\0';
        uri->userinfo = sdsnew(s);
        if (uri->userinfo == null)
        {
            return UV_ENOMEM;
        }
    }
    else
    {
        cp = s;
    }
    /* Optionally, we end with ":port" */
    for (port = eos - 1; port >= cp && ISDIGIT(*port); --port)
    {
        ;
    }
    if (port >= cp && *port == ':')
    {
        if (port + 1 == eos) /* Leave port unspecified; the RFC allows a nil port */
        {
            uri->port = 80;
        }
        else if ((uri->port = parse_port(port + 1, eos)) < 0)
        {
            return UV_EINVAL;
        }
        eos = port;
    }
    /* Now, cp..eos holds the "host" port, which can be an IPv4Address,
     * an IP-Literal, or a reg-name */
    ASSERT(eos >= cp);
    if (*cp == '[' && eos >= cp + 2 && *(eos - 1) == ']')
    {
        /* IPv6address, IP-Literal, or junk. */
        if (!bracket_addr_ok(cp, eos))
        {
            return UV_EINVAL;
        }
    }
    else
    {
        /* Make sure the host part is ok. */
        if (!regname_ok(cp, eos)) /* Match IPv4Address or reg-name */
        {
            return UV_EINVAL;
        }
    }
    uri->host = sdsnewlen(cp, eos - cp);
    if (uri->host == null)
    {
        return UV_ENOMEM;
    }
    return 0;
}

enum uri_part
{
    PART_PATH, PART_QUERY, PART_FRAGMENT
};

/* Return the character after the longest prefix of 'cp' that matches...
 *   *pchar / "/" if allow_qchars is false, or
 *   *(pchar / "/" / "?") if allow_qchars is true.
 */
static char* end_of_path(char *cp, enum uri_part part)
{
    while (*cp)
    {
        if (CHAR_IS_UNRESERVED(*cp) || strchr(SUBDELIMS, *cp) || *cp == ':' || *cp == '@' || *cp == '/')
        {
            ++cp;
        }
        else if (*cp == '%' && ISXDIGIT(cp[1]) && ISXDIGIT(cp[2]))
        {
            cp += 3;
        }
        else if (*cp == '?' && part != PART_PATH)
        {
            ++cp;
        }
        else
        {
            return cp;
        }
    }
    return cp;
}

static int path_matches_noscheme(const char *cp)
{
    while (*cp)
    {
        if (*cp == ':')
        {
            return 0;
        }
        else if (*cp == '/')
        {
            return 1;
        }
        ++cp;
    }
    return 1;
}

int uv_http_uri_parser(const char* uri_src, uv_http_uri_t* uri)
{
    int error = 0;
    char *readbuf = null, *readp = null, *token = null, *query = null;
    char *path = null, *fragment = null;
    int got_authority = 0;

    uri->port = 80;

    do
    {
        readbuf = strdup(uri_src);
        if (readbuf == null)
        {
            error = UV_ENOMEM;
            break;;
        }

        readp = readbuf;
        token = null;

        /* We try to follow RFC3986 here as much as we can, and match
         the productions

         URI = scheme ":" hier-part [ "?" query ] [ "#" fragment ]

         relative-ref  = relative-part [ "?" query ] [ "#" fragment ]
         */

        /* 1. scheme: */
        token = strchr(readp, ':');
        if (token && scheme_ok(readp, token))
        {
            *token = '\0';
            uri->scheme = sdsnew(readp);
            if (uri->scheme == null)
            {
                error = UV_ENOMEM;
                break;
            }
            readp = token + 1; /* eat : */
        }

        /* 2. Optionally, "//" then an 'authority' part. */
        if (readp[0] == '/' && readp[1] == '/')
        {
            char* authority = null;
            readp += 2;
            authority = readp;
            path = end_of_authority(readp);
            if (parse_authority(uri, authority, path) < 0)
            {
                error = UV_EINVAL;
                break;
            }
            readp = path;
            got_authority = 1;
        }

        /* 3. Query: path-abempty, path-absolute, path-rootless, or path-empty
         */
        path = readp;
        readp = end_of_path(path, PART_PATH);

        /* Query */
        if (*readp == '?')
        {
            *readp = '\0';
            ++readp;
            query = readp;
            readp = end_of_path(readp, PART_QUERY);
        }

        /* fragment */
        if (*readp == '#')
        {
            *readp = '\0';
            ++readp;
            fragment = readp;
            readp = end_of_path(readp, PART_FRAGMENT);
        }

        if (*readp != '\0')
        {
            error = UV_EINVAL;
            break;
        }

        /* These next two cases may be unreachable; I'm leaving them
         * in to be defensive. */
        /* If you didn't get an authority, the path can't begin with "//" */
        if (!got_authority && path[0] == '/' && path[1] == '/')
        {
            error = UV_EINVAL;
            break;
        }
        /* If you did get an authority, the path must begin with "/" or be
         * empty. */
        if (got_authority && path[0] != '/' && path[0] != '\0')
        {
            error = UV_EINVAL;
            break;
        }
        /* (End of maybe-unreachable cases) */

        /* If there was no scheme, the first part of the path (if any) must
         * have no colon in it. */
        if (!uri->scheme && !path_matches_noscheme(path))
        {
            error = UV_EINVAL;
            break;
        }

        ASSERT(path);
        uri->path = sdsnew(path);
        if (uri->path == null)
        {
            error = UV_ENOMEM;
            break;
        }

        if (query)
        {
            uri->query = sdsnew(query);
            if (uri->query == null)
            {
                error = UV_ENOMEM;
                break;
            }
        }
        if (fragment)
        {
            uri->fragment = sdsnew(fragment);
            if (uri->fragment == null)
            {
                error = UV_ENOMEM;
                break;
            }
        }

        free(readbuf);
        readbuf = null;

        return 0;

    } while (0);

    if (readbuf)
    {
        uv_http_uri_clean(uri);
        free(readbuf);
    }

    return error;
}

void uv_http_uri_clean(uv_http_uri_t* http_uri)
{

#define HTTP_URI_OPT_FREE(u) \
    if(u) \
    { \
        sdsfree(u); \
        u = null; \
    }

    HTTP_URI_OPT_FREE(http_uri->scheme)
    HTTP_URI_OPT_FREE(http_uri->userinfo)
    HTTP_URI_OPT_FREE(http_uri->host)
    HTTP_URI_OPT_FREE(http_uri->path)
    HTTP_URI_OPT_FREE(http_uri->query)
    HTTP_URI_OPT_FREE(http_uri->fragment)

#undef HTTP_URI_OPT_FREE
}

#define UV_HTTP_STRERROR_GEN(CODE, NAME, STR) case UV_HTTP_ ## NAME: return STR;
const char* uv_http_status_code_str(int status)
{
    switch (status)
    {
    UV_HTTP_STATUS_CODE_MAP(UV_HTTP_STRERROR_GEN)
    default:
        return "Unknown status code";
    }
}
#undef UV_HTTP_STRERROR_GEN
