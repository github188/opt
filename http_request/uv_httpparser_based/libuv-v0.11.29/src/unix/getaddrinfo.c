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

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <stddef.h> /* NULL */
#include <stdlib.h>
#include <string.h>

#include "uv.h"
#include "internal.h"
#include "uv-dns.h"
#include "uv-nameser.h"

//#define UV_GETADDRINFO_DEBUG
#ifdef UV_GETADDRINFO_DEBUG
#  define UV_GETADDRINFO_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#  define UV_GETADDRINFO_LOG(...)
#endif

#define EDNSFIXEDSZ                   11    /* Size of EDNS header */
#define UV__DNS_D_MAXNAME             255
#define UV__DNS_PORT                  53
#define UV__DNS_TIMEOUT               15000
#define UV__GETADDRINFO_STEP_HOSTS    0
#define UV__GETADDRINFO_STEP_DNS      1
#define UV__PATH_HOSTS                "/etc/hosts"
#define UV__PATH_RESOLV               "/etc/resolv.conf"
#define UV__DNS_HOSTS_ISCOM(ch)       ((ch) == '#' || (ch) == ';')

#ifndef MAX
#    define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

typedef struct uv__dnsserver_s
{
    uv_addr_t addr;
    int status;    // 0: none, 1: udp done, 2: tcp done
    int tcp_done;
    void* dns_server_queue[2];
} uv__dnsserver_t;

int uv__getaddrinfo_translate_error(int sys_err)
{
    switch (sys_err)
    {
    case 0:
        return 0;
#if defined(EAI_ADDRFAMILY)
        case EAI_ADDRFAMILY: return UV_EAI_ADDRFAMILY;
#endif
#if defined(EAI_AGAIN)
        case EAI_AGAIN: return UV_EAI_AGAIN;
#endif
#if defined(EAI_BADFLAGS)
        case EAI_BADFLAGS: return UV_EAI_BADFLAGS;
#endif
#if defined(EAI_BADHINTS)
        case EAI_BADHINTS: return UV_EAI_BADHINTS;
#endif
#if defined(EAI_CANCELED)
        case EAI_CANCELED: return UV_EAI_CANCELED;
#endif
#if defined(EAI_FAIL)
        case EAI_FAIL: return UV_EAI_FAIL;
#endif
#if defined(EAI_FAMILY)
        case EAI_FAMILY: return UV_EAI_FAMILY;
#endif
#if defined(EAI_MEMORY)
        case EAI_MEMORY: return UV_EAI_MEMORY;
#endif
#if defined(EAI_NODATA)
        case EAI_NODATA: return UV_EAI_NODATA;
#endif
#if defined(EAI_NONAME)
# if !defined(EAI_NODATA) || EAI_NODATA != EAI_NONAME
        case EAI_NONAME: return UV_EAI_NONAME;
# endif
#endif
#if defined(EAI_OVERFLOW)
        case EAI_OVERFLOW: return UV_EAI_OVERFLOW;
#endif
#if defined(EAI_PROTOCOL)
        case EAI_PROTOCOL: return UV_EAI_PROTOCOL;
#endif
#if defined(EAI_SERVICE)
        case EAI_SERVICE: return UV_EAI_SERVICE;
#endif
#if defined(EAI_SOCKTYPE)
        case EAI_SOCKTYPE: return UV_EAI_SOCKTYPE;
#endif
#if defined(EAI_SYSTEM)
        case EAI_SYSTEM: return -errno;
#endif
    }
    assert(!"unknown EAI_* error code");
    abort();
    return 0; /* Pacify compiler. */
}

static void uv__print_addrres(struct addrinfo *res, unsigned int n)
{
    struct addrinfo* curr = null;
    char addrstring[NI_MAXHOST];

    for (curr = res; curr; curr = curr->ai_next)
    {
        uv_ip_name((uv_addr_t*) (curr->ai_addr), addrstring, sizeof(addrstring));
    }
}

static void uv__free_addrres(struct addrinfo *res)
{
    struct addrinfo* curr = res;
    struct addrinfo* temp = null;

    while (curr)
    {
        if (curr->ai_canonname)
        {
            free(curr->ai_canonname);
            curr->ai_canonname = null;
        }
        if (curr->ai_addr)
        {
            free(curr->ai_addr);
            curr->ai_addr = null;
        }

        temp = curr;
        curr = temp->ai_next;
        free(temp);
    }
}

static struct addrinfo* uv__new_addrres_node(const unsigned char* aptr, int family, int port)
{
    uv_addr_t* uv_addr = null;
    struct addrinfo* node = calloc(1, sizeof(struct addrinfo));

    if (node)
    {
        uv_addr = calloc(1, sizeof(uv_addr_t));

        if (uv_addr)
        {
            memcpy(&uv_addr->addr4.sin_addr, aptr, sizeof(struct in_addr));
            uv_addr->addr.sa_family = family;
            uv_addr->addr4.sin_port = htons(port);

            node->ai_addrlen = sizeof(uv_addr_t);
            node->ai_addr = (struct sockaddr*) uv_addr;
        }
        else
        {
            free(node);
            node = null;
        }
    }

    return node;
}

static int uv__get_hostent(FILE *fp, int port, struct addrinfo** res)
{
    char line[1024] = "";
    char *p = null, *q = null;
    char *txtaddr = null, *txthost = null, *txtalias = null;
    int status = 0;
    uv_addr_t* uvaddr = null;
    size_t naliases = 0;
    struct addrinfo* _addrinfo = null;

    *res = null; /* assume failure */

    while (!feof(fp))
    {
        fgets(line, sizeof(line), fp);
        /* trim line comment. */
        p = line;
        while (*p && (*p != '#'))
        {
            p++;
        }
        *p = '\0';

        /* Trim trailing whitespace. */
        q = p - 1;
        while ((q >= line) && ISSPACE(*q))
        {
            q--;
        }
        *++q = '\0';

        /* Skip leading whitespace. */
        p = line;
        while (*p && ISSPACE(*p))
        {
            p++;
        }
        if (!*p)
        {
            /* Ignore line if empty. */
            continue;
        }

        /* Pointer to start of IPv4 or IPv6 address part. */
        txtaddr = p;

        /* Advance past address part. */
        while (*p && !ISSPACE(*p))
        {
            p++;
        }
        if (!*p)
        {
            /* Ignore line if reached end of line. */
            continue;
        }

        /* Null terminate address part. */
        *p = '\0';

        /* Advance to host name */
        p++;
        while (*p && ISSPACE(*p))
        {
            p++;
        }
        if (!*p)
        {
            /* Ignore line if reached end of line. */
            continue;
        }

        /* Pointer to start of host name. */
        txthost = p;

        /* Advance past host name. */
        while (*p && !ISSPACE(*p))
        {
            p++;
        }

        /* Pointer to start of first alias. */
        txtalias = null;
        if (*p)
        {
            q = p + 1;
            while (*q && ISSPACE(*q))
            {
                q++;
            }
            if (*q)
            {
                txtalias = q;
            }
        }

        /* Null terminate host name. */
        *p = '\0';

        /* find out number of aliases. */
        naliases = 0;
        if (txtalias)
        {
            p = txtalias;
            while (*p)
            {
                while (*p && !ISSPACE(*p))
                {
                    p++;
                }
                while (*p && ISSPACE(*p))
                {
                    p++;
                }
                naliases++;
            }
        }

#if 0
        addr.addr.family = AF_UNSPEC;
        addr.addr4.s_addr = INADDR_NONE;

        if (family == AF_INET || family == AF_UNSPEC)
        {
            addr.addr4.s_addr = inet_addr(txtaddr);
            if (addr.addr4.s_addr != INADDR_NONE)
            {
                /* Actual network address family and length. */
                addr.addr.family = AF_INET;
                addrlen = sizeof(addr.addr4);
            }
        }
        if ((family == AF_INET6) || ((family == AF_UNSPEC) && (!addrlen)))
        {
            if (inet_pton(AF_INET6, txtaddr, &addr.addr6) > 0)
            {
                /* Actual network address family and length. */
                addr.addr.family = AF_INET6;
                addrlen = sizeof(addr.addr6);
            }
        }
        if (!addrlen)
        {
            /* Ignore line if invalid address string for the requested family. */
            continue;
        }
#endif

        /*
         ** Actual address family possible values are AF_INET and AF_INET6 only.
         */

        /* Allocate memory for the hostent structure. */
        _addrinfo = malloc(sizeof(struct addrinfo));
        if (!_addrinfo)
        {
            break;
        }

        /* Initialize fields for out of memory condition. */
//        hostent->h_aliases = null;
//        hostent->h_addr_list = null;
        /* Copy official host name. */
        _addrinfo->ai_canonname = strdup(txthost);
        if (!_addrinfo->ai_canonname)
        {
            break;
        }

        /* Copy network address. */
//        if (!hostent->h_addr_list[0])
//        {
//            break;
//        }
        _addrinfo->ai_next = null;
        _addrinfo->ai_addrlen = sizeof(uv_addr_t);
        _addrinfo->ai_addr = calloc(1, _addrinfo->ai_addrlen);
        if (!_addrinfo->ai_addr)
        {
            break;
        }
        uvaddr = (uv_addr_t*) _addrinfo->ai_addr;

        /* convert address string to network address for the requested family. */
        if (uv_ip_addr(txtaddr, port, uvaddr))
        {
            break;
        }
        _addrinfo->ai_family = uvaddr->addr.sa_family;

#if 0
        /* copy aliases. */
        hostent->h_aliases = calloc(1, (naliases + 1) * sizeof(char *));
        if (!hostent->h_aliases)
        {
            break;
        }

        alias = hostent->h_aliases;
        while (naliases)
        {
            *(alias + naliases--) = null;
        }
        *alias = null;
        while (txtalias)
        {
            p = txtalias;
            while (*p && !ISSPACE(*p))
            {
                p++;
            }
            q = p;
            while (*q && ISSPACE(*q))
            {
                q++;
            }
            *p = '\0';
            if ((*alias = strdup(txtalias)) == null)
            {
                break;
            }
            alias++;
            txtalias = *q ? q : null;
        }
        if (txtalias)
        {
            /* alias memory allocation failure. */
            break;
        }

        /* Copy actual network address family and length. */
        hostent->h_addrtype = addr.addr.sa_family;
        hostent->h_length = sizeof(uv_addr_t);
#endif

        /* Return hostent successfully */
        *res = _addrinfo;
        return 0;

    }

    /* If allocated, free line buffer. */

    if (status == 0)
    {
        /* Memory allocation failure; clean up. */
        if (_addrinfo)
        {
            uv__free_addrres(_addrinfo);
        }
        return UV_ENOMEM;
    }

    return status;
}

/*
 * fixme use uv_fs_* to async
 */
static struct addrinfo* uv__getaddrinfo_file_lookup(const char* name, int port)
{
//    char** ap = null;
    struct addrinfo* res = null;
//    char addrstring[NI_MAXHOST];
    FILE* fp = null;
    int status = 0;
//    int error = 0;

    fp = fopen(UV__PATH_HOSTS, "r");
    if (!fp)
    {
        return null;
    }
    while ((status = uv__get_hostent(fp, port, &res)) == 0)
    {
        if (strcasecmp(res->ai_canonname, name) == 0)
        {
            break;
        }
        uv__free_addrres(res);
    }
    fclose(fp);
    if (status != 0)
    {
        res = null;
    }
    return res;
}

static void uv__dns_free_servers(uv_getaddrinfo_t* req)
{
    QUEUE* q;
    uv__dnsserver_t* s = null;

    while (!QUEUE_EMPTY(&req->dns_server_queue))
    {
        q = QUEUE_HEAD(&req->dns_server_queue);
        QUEUE_REMOVE(q);
        s = QUEUE_DATA(q, uv__dnsserver_t, dns_server_queue);
        free(s);
    }
}

static void uv_getaddrinfo_udp_close(uv_getaddrinfo_t* req)
{

    if (uv_close((uv_handle_t*) req->udp) == 0)
    {

        free(req->udp);
        req->udp = NULL;

    }
}

static void uv_getaddrinfo_tcp_close(uv_getaddrinfo_t* req)
{

    if (uv_close((uv_handle_t*) req->tcp) == 0)
    {

        free(req->tcp);
        req->tcp = NULL;

    }
}

static void uv__getaddrinfo_end(uv_getaddrinfo_t* req, int status)
{
    struct addrinfo* res = req->res;
    req->res = null;

    if (req->hostname)
    {

        free(req->hostname);
    }

    uv_close((uv_handle_t*) &req->timer);

    uv__dns_free_servers(req);

    uv__req_unregister(req->loop, req);

    if (req->udp)
    {

        uv_getaddrinfo_udp_close(req);
    }

    if (req->tcp)
    {

        uv_getaddrinfo_tcp_close(req);
    }

    req->cb(req, status, res, req->data);

    if (res)
    {

        uv__free_addrres(res);
    }

}

static int uv__get_nameservers(uv_getaddrinfo_t* req)
{
    FILE* fp = null;
    char word[MAX(INET6_ADDRSTRLEN, UV__DNS_D_MAXNAME) + 1];
    unsigned wp, wc, skip;
    int ch;
    uv__dnsserver_t* s;

    QUEUE_INIT(&req->dns_server_queue);

    fp = fopen(UV__PATH_RESOLV, "r");

    if (!fp)
    {
        return ferror(fp);
    }
    do
    {
        wc = 0;
        skip = 0;

        do
        {
            memset(word, '\0', sizeof(word));
            wp = 0;

            while ((ch = fgetc(fp)) != EOF && ch != '\n')
            {
                skip |= !!UV__DNS_HOSTS_ISCOM(ch);

                if (skip)
                {
                    continue;
                }

                if (ISSPACE(ch))
                {
                    break;
                }

                if (wp < sizeof(word) - 1)
                {
                    word[wp] = ch;
                }
                wp++;
            }

            if (!wp)
            {
                continue;
            }

            wc++;

            switch (wc)
            {
            case 1:
                if (strcmp(word, "nameserver"))
                {
                    skip = 1;
                }
                break;
            case 2:
                s = calloc(1, sizeof(*s));
                if (!s)
                {
                    fclose(fp);
                    return UV_EAI_NONAME;
                }

                uv_ip_addr(word, UV__DNS_PORT, &s->addr);
                QUEUE_INIT(&s->dns_server_queue);
                QUEUE_INSERT_TAIL(&req->dns_server_queue, &s->dns_server_queue);
                break;
            default:
                break;
            }
        } while (ch != EOF && ch != '\n');
    } while (ch != EOF);

    fclose(fp);
    return 0;
}

/* Expand an RFC1035-encoded domain name given by encoded.  The
 * containing message is given by abuf and alen.  The result given by
 * *s, which is set to a NUL-terminated allocated buffer.  *enclen is
 * set to the length of the encoded name (not the length of the
 * expanded name; the goal is to tell the caller how many bytes to
 * move forward to get past the encoded name).
 *
 * In the simple case, an encoded name is a series of labels, each
 * composed of a one-byte length (limited to values between 0 and 63
 * inclusive) followed by the label contents.  The name is terminated
 * by a zero-length label.
 *
 * In the more complicated case, a label may be terminated by an
 * indirection pointer, specified by two bytes with the high bits of
 * the first byte (corresponding to INDIR_MASK) set to 11.  With the
 * two high bits of the first byte stripped off, the indirection
 * pointer gives an offset from the beginning of the containing
 * message with more labels to decode.  Indirection can happen an
 * arbitrary number of times, so we have to detect loops.
 *
 * Since the expanded name uses '.' as a label separator, we use
 * backslashes to escape periods or backslashes in the expanded name.
 */

/* Return the length of the expansion of an encoded domain name, or
 * -1 if the encoding is invalid.
 */
static int name_length(const unsigned char *encoded, const unsigned char *abuf, int alen)
{
    int n = 0, offset, indir = 0;

    /* Allow the caller to pass us abuf + alen and have us check for it. */
    if (encoded >= abuf + alen)
    {
        return -1;
    }

    while (*encoded)
    {
        if ((*encoded & INDIR_MASK) == INDIR_MASK)
        {
            /* Check the offset and go there. */
            if (encoded + 1 >= abuf + alen)
            {
                return -1;
            }
            offset = (*encoded & ~INDIR_MASK) << 8 | *(encoded + 1);
            if (offset >= alen)
            {
                return -1;
            }
            encoded = abuf + offset;

            /* If we've seen more indirects than the message length,
             * then there's a loop.
             */
            if (++indir > alen)
            {
                return -1;
            }
        }
        else
        {
            offset = *encoded;
            if (encoded + offset + 1 >= abuf + alen)
            {
                return -1;
            }
            encoded++;
            while (offset--)
            {
                n += (*encoded == '.' || *encoded == '\\') ? 2 : 1;
                encoded++;
            }
            n++;
        }
    }

    /* If there were any labels at all, then the number of dots is one
     * less than the number of labels, so subtract one.
     */
    return (n) ? n - 1 : n;
}

static int uv__expand_name(const unsigned char *encoded, const unsigned char *abuf, int alen, char **s, long *enclen)
{
    int len, indir = 0;
    char *q;
    const unsigned char *p;
    union
    {
        ssize_t sig;
        size_t uns;
    } nlen;

    nlen.sig = name_length(encoded, abuf, alen);
    if (nlen.sig < 0)
    {
        printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
        return UV_EAI_BADHINTS;
    }

    *s = malloc(nlen.uns + 1);
    if (!*s)
    {
        printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
        return UV_ENOMEM;
    }
    q = *s;

    if (nlen.uns == 0)
    {
        /* RFC2181 says this should be ".": the root of the DNS tree.
         * Since this function strips trailing dots though, it becomes ""
         */
        q[0] = '\0';

        /* indirect root label (like 0xc0 0x0c) is 2 bytes long (stupid, but
         valid) */
        if ((*encoded & INDIR_MASK) == INDIR_MASK)
            *enclen = 2L;
        else
            *enclen = 1L; /* the caller should move one byte to get past this */

        return 0;
    }

    /* No error-checking necessary; it was all done by name_length(). */
    p = encoded;
    while (*p)
    {
        if ((*p & INDIR_MASK) == INDIR_MASK)
        {
            if (!indir)
            {
                *enclen = p + 2U - encoded;
                indir = 1;
            }
            p = abuf + ((*p & ~INDIR_MASK) << 8 | *(p + 1));
        }
        else
        {
            len = *p;
            p++;
            while (len--)
            {
                if (*p == '.' || *p == '\\')
                    *q++ = '\\';
                *q++ = *p;
                p++;
            }
            *q++ = '.';
        }
    }
    if (!indir)
    {
        *enclen = p + 1U - encoded;
    }

    /* Nuke the trailing period if we wrote one. */
    if (q > *s)
    {
        *(q - 1) = 0;
    }
    else
    {
        *q = 0; /* zero terminate */
    }

    return 0;
}

struct ares_addrttl
{
    struct in_addr ipaddr;
    int ttl;
};

static int uv__parse_a_reply(const unsigned char* abuf, int alen, int port, struct addrinfo **res,
                             struct ares_addrttl *addrttls, int *naddrttls)
{
    unsigned int i = 0, qdcount = 0, ancount = 0;
    int status = 0, rr_type = 0;
    int rr_class = 0, rr_len = 0;
    int rr_ttl = 0, naddrs = 0;
    int cname_ttl = INT_MAX; /* the TTL imposed by the CNAME chain */
    int naliases = 0;
    long len = 0;
    const unsigned char *aptr = null;
    char *hostname = null, *rr_name = null;
    char *rr_data = null, **aliases = null;
    const int max_addr_ttls = (addrttls && naddrttls) ? *naddrttls : 0;
    struct addrinfo* _addrinfo_prev = null;
    struct addrinfo* _addrinfo_curr = null;

    assert(*res == null);

    /* Same with *naddrttls. */
    if (naddrttls)
    {
        *naddrttls = 0;
    }

    do
    {
        /* Give up if abuf doesn't have room for a header. */
        if (alen < HFIXEDSZ)
        {
            printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
            status = UV_EAI_NONAME;
            break;
        }

        /* Fetch the question and answer count from the header. */
        qdcount = DNS_HEADER_QDCOUNT(abuf);
        ancount = DNS_HEADER_ANCOUNT(abuf);

        if (qdcount != 1 || ancount == 0)
        {
            printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
            status = UV_EAI_NONAME;
            break;
        }

        /* Expand the name from the question, and skip past the question. */
        aptr = abuf + HFIXEDSZ;
        status = uv__expand_name(aptr, abuf, alen, &hostname, &len);
        if (status != 0)
        {
            printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
            break;
        }

        if (aptr + len + QFIXEDSZ > abuf + alen)
        {
            printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
            status = UV_EAI_NONAME;
            break;
        }
        aptr += len + QFIXEDSZ;

        /* allocate addresses and aliases; ancount gives an upper bound for both. */
        aliases = calloc((ancount + 1), sizeof(char *));
        if (!aliases)
        {
            printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
            status = UV_ENOMEM;
            break;
        }

        naddrs = 0;
        naliases = 0;

        /* Examine each answer resource record (RR) in turn. */
        for (i = 0; i < ancount; i++)
        {
            /* Decode the RR up to the data field. */
            status = uv__expand_name(aptr, abuf, alen, &rr_name, &len);
            if (status != 0)
            {
                if (rr_name)
                {
                    free(rr_name);
                    rr_name = null;
                }
                printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
                break;
            }
            aptr += len;
            if (aptr + RRFIXEDSZ > abuf + alen)
            {
                if (rr_name)
                {
                    free(rr_name);
                    rr_name = null;
                }
                printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
                status = UV_EAI_NONAME;
                break;
            }
            rr_type = DNS_RR_TYPE(aptr);
            rr_class = DNS_RR_CLASS(aptr);
            rr_len = DNS_RR_LEN(aptr);
            rr_ttl = DNS_RR_TTL(aptr);
            aptr += RRFIXEDSZ;
            if (aptr + rr_len > abuf + alen)
            {
                if (rr_name)
                {
                    free(rr_name);
                    rr_name = null;
                }
                printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
                status = UV_EAI_NONAME;
                break;
            }

            if (rr_class == C_IN && rr_type == T_A && rr_len == sizeof(struct in_addr) && strcasecmp(rr_name, hostname) == 0)
            {
                if (aptr + sizeof(struct in_addr) > abuf + alen)
                {
                    if (rr_name)
                    {
                        free(rr_name);
                        rr_name = null;
                    }
                    printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
                    status = UV_EAI_NONAME;
                    break;
                }

                _addrinfo_curr = uv__new_addrres_node(aptr, AF_INET, port);
                if (!_addrinfo_curr)
                {
                    if (rr_name)
                    {
                        free(rr_name);
                        rr_name = null;
                    }
                    printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
                    status = UV_ENOMEM;
                    break;
                }

                if (!(*res))
                {
                    *res = _addrinfo_curr;
                }

                if (_addrinfo_prev)
                {
                    _addrinfo_prev->ai_next = _addrinfo_curr;
                }

                _addrinfo_prev = _addrinfo_curr;

                uv__print_addrres(*res, __LINE__);

                if (naddrs < max_addr_ttls)
                {
                    struct ares_addrttl* const at = &addrttls[naddrs];
                    if (aptr + sizeof(struct in_addr) > abuf + alen)
                    {
                        if (rr_name)
                        {
                            free(rr_name);
                            rr_name = null;
                        }
                        printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
                        status = UV_EAI_NONAME;
                        break;
                    }
                    memcpy(&at->ipaddr, aptr, sizeof(struct in_addr));
                    at->ttl = rr_ttl;
                }
                naddrs++;
                status = 0;
            }

            if (rr_class == C_IN && rr_type == T_CNAME)
            {
                /* Record the RR name as an alias. */
                if (aliases)
                {
                    aliases[naliases] = rr_name;
                    rr_name = null;
                }
                else
                {
                    free(rr_name);
                    rr_name = null;
                }

                naliases++;

                /* Decode the RR data and replace the hostname with it. */
                status = uv__expand_name(aptr, abuf, alen, &rr_data, &len);
                if (status != 0)
                {
                    if (rr_data)
                    {
                        free(rr_data);
                        rr_data = null;
                    }
                    printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
                    break;
                }

                if (hostname)
                {
                    free(hostname);
                    hostname = null;
                }
                hostname = rr_data;
                rr_data = null;

                /* Take the min of the TTLs we see in the CNAME chain. */
                if (cname_ttl > rr_ttl)
                {
                    cname_ttl = rr_ttl;
                }
            }
            else
            {
                free(rr_name);
                rr_name = null;
            }

            aptr += rr_len;
            if (aptr > abuf + alen)
            {
                printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
                status = UV_EAI_NONAME;
                break;
            }
        }

        if (status == 0 && naddrs == 0 && naliases == 0)
        {
            /* the check for naliases to be zero is to make sure CNAME responses
             don't get caught here */
            status = UV_EAI_NODATA;
            printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
            break;
        }

        if (status == 0)
        {
            /* We got our answer. */
            if (naddrttls)
            {
                const int n = naddrs < max_addr_ttls ? naddrs : max_addr_ttls;
                for (i = 0; i < n; i++)
                {
                    /* Ensure that each A TTL is no larger than the CNAME TTL. */
                    if (addrttls[i].ttl > cname_ttl)
                    {
                        addrttls[i].ttl = cname_ttl;
                    }
                }
                *naddrttls = n;
            }

            if (aliases)
            {
                aliases[naliases] = null;
            }

            if (aliases)
            {
                for (i = 0; i < naliases; i++)
                {
                    free(aliases[i]);
                    aliases[i] = null;
                }
                free(aliases);
                aliases = null;
            }

            if (hostname)
            {
                free(hostname);
                hostname = null;
            }
        }
        else
        {
            printf("dns parse failed. file: %s, line: %u\n", __FILE__, __LINE__);
            break;
        }

        assert(status == 0);

        return 0;

    } while (0);

    if (aliases)
    {
        for (i = 0; i < naliases; i++)
        {
            free(aliases[i]);
            aliases[i] = null;
        }
        free(aliases);
        aliases = null;
    }

    if (hostname)
    {
        free(hostname);
        hostname = null;
    }

    uv__free_addrres(*res);
    *res = null;

    return status;
}

/* Header format, from RFC 1035:
 *                                  1  1  1  1  1  1
 *    0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |                      ID                       |
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |QR|   Opcode  |AA|TC|RD|RA|   Z    |   RCODE   |
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |                    QDCOUNT                    |
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |                    ANCOUNT                    |
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |                    NSCOUNT                    |
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |                    ARCOUNT                    |
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * AA, TC, RA, and RCODE are only set in responses.  Brief description
 * of the remaining fields:
 *      ID      Identifier to match responses with queries
 *      QR      Query (0) or response (1)
 *      Opcode  For our purposes, always QUERY
 *      RD      Recursion desired
 *      Z       Reserved (zero)
 *      QDCOUNT Number of queries
 *      ANCOUNT Number of answers
 *      NSCOUNT Number of name server records
 *      ARCOUNT Number of additional records
 *
 * Question format, from RFC 1035:
 *                                  1  1  1  1  1  1
 *    0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |                                               |
 *  /                     QNAME                     /
 *  /                                               /
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |                     QTYPE                     |
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  |                     QCLASS                    |
 *  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * The query name is encoded as a series of labels, each represented
 * as a one-byte length (maximum 63) followed by the text of the
 * label.  The list is terminated by a label of length zero (which can
 * be thought of as the root domain).
 */

static int uv__dns_create_query(const char *name, int dnsclass, int type, unsigned short id, unsigned char* buf, int *buflen)
{
    int len;
    unsigned char *q;
    const char *p;

    /* Set our results early, in case we bail out early with an error. */
    *buflen = 0;

    /* Compute the length of the encoded name so we can check buflen.
     * Start counting at 1 for the zero-length label at the end. */
    len = 1;
    for (p = name; *p; p++)
    {
        if (*p == '\\' && *(p + 1) != 0)
        {
            p++;
        }
        len++;
    }
    /* If there are n periods in the name, there are n + 1 labels, and
     * thus n + 1 length fields, unless the name is empty or ends with a
     * period.  So add 1 unless name is empty or ends with a period.
     */
    if (*name && *(p - 1) != '.')
    {
        len++;
    }

    /* Immediately reject names that are longer than the maximum of 255
     * bytes that's specified in RFC 1035 ("To simplify implementations,
     * the total length of a domain name (i.e., label octets and label
     * length octets) is restricted to 255 octets or less."). We aren't
     * doing this just to be a stickler about RFCs. For names that are
     * too long, 'dnscache' closes its TCP connection to us immediately
     * (when using TCP) and ignores the request when using UDP, and
     * BIND's named returns ServFail (TCP or UDP). Sending a request
     * that we know will cause 'dnscache' to close the TCP connection is
     * painful, since that makes any other outstanding requests on that
     * connection fail. And sending a UDP request that we know
     * 'dnscache' will ignore is bad because resources will be tied up
     * until we time-out the request.
     */
    if (len > MAXCDNAME)
    {
        return UV_EAI_BADHINTS;
    }

    *buflen = len + HFIXEDSZ + QFIXEDSZ + EDNSFIXEDSZ;

    /* Set up the header. */
    q = buf;
    memset(q, 0, HFIXEDSZ);
    DNS_HEADER_SET_QID(q, id);
    DNS_HEADER_SET_OPCODE(q, QUERY);
    DNS_HEADER_SET_RD(q, 1);
    DNS_HEADER_SET_QDCOUNT(q, 1);

    if (EDNSFIXEDSZ)
    {
        DNS_HEADER_SET_ARCOUNT(q, 1);
    }

    /* A name of "." is a screw case for the loop below, so adjust it. */
    if (strcmp(name, ".") == 0)
    {
        name++;
    }

    /* Start writing out the name after the header. */
    q += HFIXEDSZ;
    while (*name)
    {
        if (*name == '.')
        {
            return UV_EAI_BADHINTS;
        }

        /* Count the number of bytes in this label. */
        len = 0;
        for (p = name; *p && *p != '.'; p++)
        {
            if (*p == '\\' && *(p + 1) != 0)
            {
                p++;
            }
            len++;
        }
        if (len > MAXLABEL)
        {
            return UV_EAI_BADHINTS;
        }

        /* Encode the length and copy the data. */
        *q++ = (unsigned char) len;
        for (p = name; *p && *p != '.'; p++)
        {
            if (*p == '\\' && *(p + 1) != 0)
            {
                p++;
            }
            *q++ = *p;
        }

        /* Go to the next label and repeat, unless we hit the end. */
        if (!*p)
        {
            break;
        }
        name = p + 1;
    }

    /* Add the zero-length label at the end. */
    *q++ = 0;

    /* Finish off the question with the type and class. */
    DNS_QUESTION_SET_TYPE(q, type);
    DNS_QUESTION_SET_CLASS(q, dnsclass);

    if (EDNSFIXEDSZ)
    {
        q += QFIXEDSZ;
        memset(q, 0, EDNSFIXEDSZ);
        q++;
        DNS_RR_SET_TYPE(q, T_OPT);
        DNS_RR_SET_CLASS(q, EDNSFIXEDSZ);
    }

    return 0;
}

static void uv__getaddrinfo_dns_query_next(uv_getaddrinfo_t* req, int status);

//void uv__getaddrinfo_on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf, void* user_data)
//{
//    uv_getaddrinfo_t* req = container_of(handle, uv_getaddrinfo_t, uv);
//    buf->base = req->qbuf;
//    buf->len = sizeof(req->qbuf);
//}

static void uv__getaddrinfo_udp_on_recv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr,
                                        unsigned flags, void* user_data)
{

    int status = nread;
    uv_getaddrinfo_t* req = (uv_getaddrinfo_t*) user_data;
    uv_timer_stop(&req->timer);

    if (nread == UV_ECANCELED) /* do not query next and do not call callback when CANCELED */
    {

        return;
    }

    printf("dns udp recv length: %d\n", nread);
    if (status > 0)
    {

        status = uv__parse_a_reply((const unsigned char*) buf->base, nread, req->port, &req->res, NULL, NULL);

        if (status)
        {

            uv__getaddrinfo_dns_query_next(req, status);

        }
        else
        {

            uv__getaddrinfo_end(req, status);

        }
    }
    else
    {
        status = nread;
        uv_getaddrinfo_udp_close(req);

        uv__getaddrinfo_dns_query_next(req, status);

    }
}

static void uv__getaddrinfo_tcp_on_read(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf, void* user_data)
{

    int status = 0;
    uv_getaddrinfo_t* req = (uv_getaddrinfo_t*) user_data;
    uv_timer_stop(&req->timer);

    if (nread == UV_ECANCELED) /* do not query next and do not call callback when CANCELED */
    {

        return;
    }

    printf("dns udp recv length: %d\n", nread);

    if (nread > 0)
    {

        status = uv__parse_a_reply((const unsigned char*) buf->base, nread, req->port, &req->res, null, null);
        if (status)
        {

            uv__getaddrinfo_dns_query_next(req, status);

        }
        else
        {

            uv__getaddrinfo_end(req, status);

        }
    }
    else
    {
        status = nread;

        uv__getaddrinfo_dns_query_next(req, status);

    }
}

static void uv__getaddrinfo_timer_udp_cb(uv_timer_t* timer)
{

    uv_getaddrinfo_t* req = container_of(timer, uv_getaddrinfo_t, timer);

    uv_timer_stop(&req->timer);

    uv__getaddrinfo_dns_query_next(req, UV_ETIMEDOUT);

}

static void uv__getaddrinfo_timer_tcp_cb(uv_timer_t* timer)
{

    uv_getaddrinfo_t* req = container_of(timer, uv_getaddrinfo_t, timer);

    uv_timer_stop(&req->timer);

    uv__getaddrinfo_dns_query_next(req, UV_ETIMEDOUT);

}

static void uv__getaddrinfo_on_udp_send(uv_udp_t* handle, int status, const uv_buf_t bufs[], unsigned int nbufs,
                                        const struct sockaddr* addr, void* user_data)
{

    uv_buf_t rbuf;
    uv_getaddrinfo_t* req = (uv_getaddrinfo_t*) user_data;
    uv_timer_stop(&req->timer);

    if (status)
    {

        if (status != UV_ECANCELED) /* do not query next and do not call callback when CANCELED */
        {

            uv__getaddrinfo_dns_query_next(req, status);

        }
    }
    else
    {

        rbuf.base = req->qbuf;
        rbuf.len = sizeof(req->qbuf);
        uv_timer_start(&req->timer, uv__getaddrinfo_timer_udp_cb, UV__DNS_TIMEOUT, 0);
        uv_udp_recv(req->udp, &rbuf, uv__getaddrinfo_udp_on_recv, req);

    }
}

static void uv__getaddrinfo_on_tcp_send(uv_stream_t* handle, int status, const uv_buf_t bufs[], unsigned int nbufs,
                                        void* user_data)
{

    uv_buf_t rbuf;
    uv_getaddrinfo_t* req = (uv_getaddrinfo_t*) user_data;
    uv_timer_stop(&req->timer);

    if (status)
    {

        if (status != UV_ECANCELED) /* do not query next and do not call callback when CANCELED */
        {

            uv__getaddrinfo_dns_query_next(req, status);

        }
    }
    else
    {
        rbuf.base = req->qbuf;
        rbuf.len = sizeof(req->qbuf);

        uv_timer_start(&req->timer, uv__getaddrinfo_timer_tcp_cb, UV__DNS_TIMEOUT, 0);
        uv_read((uv_stream_t*) req->tcp, &rbuf, uv__getaddrinfo_tcp_on_read, req);

    }
}

static void uv__getaddrinfo_udp_query(uv_getaddrinfo_t* req, uv__dnsserver_t* s)
{
    int error = 0;
    uv_buf_t uvbuf;

    error = uv__dns_create_query(req->hostname, C_IN, T_A, req->id, (unsigned char*) req->qbuf, &req->qlen);
    if (error)
    {

        uv__getaddrinfo_end(req, error);
        return;
    }

    req->udp = calloc(1, sizeof(*req->udp));
    if (!req->udp)
    {

        uv__getaddrinfo_end(req, UV_EAI_NONAME);
        return;
    }

    uv_udp_init(req->loop, req->udp);

    uvbuf = uv_buf_init(req->qbuf, req->qlen);

    uv_timer_start(&req->timer, uv__getaddrinfo_timer_udp_cb, UV__DNS_TIMEOUT, 0);
    uv_udp_send(req->udp, &uvbuf, 1, &s->addr.addr, uv__getaddrinfo_on_udp_send, req);

}

static void uv__getaddrinfo_on_tcp_connect(uv_tcp_t* handle, int status, void* user_data)
{

    int error = 0;
    uv_buf_t uvbuf;
    uv_getaddrinfo_t* req = (uv_getaddrinfo_t*) user_data;
    uv_timer_stop(&req->timer);

    if (status)
    {

        if (status != UV_ECANCELED) /* do not query next and do not call callback when CANCELED */
        {

            uv__getaddrinfo_dns_query_next(req, status);

        }
    }
    else
    {

        error = uv__dns_create_query(req->hostname, C_IN, T_A, req->id, (unsigned char*) req->qbuf, &req->qlen);

        if (error)
        {

            uv__getaddrinfo_end(req, error);

            return;
        }

        uvbuf = uv_buf_init(req->qbuf, req->qlen);
        uv_timer_start(&req->timer, uv__getaddrinfo_timer_tcp_cb, UV__DNS_TIMEOUT, 0);
        uv_write((uv_stream_t*) req->tcp, &uvbuf, 1, uv__getaddrinfo_on_tcp_send, req);

    }
}

static void uv__getaddrinfo_tcp_query(uv_getaddrinfo_t* req, uv__dnsserver_t* s)
{

    req->tcp = calloc(1, sizeof(*req->tcp));
    if (!req->tcp)
    {

        uv__getaddrinfo_end(req, UV_EAI_NONAME);

        return;
    }

    uv_tcp_init(req->loop, req->tcp);
    uv_timer_start(&req->timer, uv__getaddrinfo_timer_tcp_cb, UV__DNS_TIMEOUT, 0);
    uv_tcp_connect(req->tcp, &s->addr.addr, uv__getaddrinfo_on_tcp_connect, req);

}

static void uv__getaddrinfo_dns_query_next(uv_getaddrinfo_t* req, int status)
{
    QUEUE* q;
    uv__dnsserver_t* s = null;

    if (req->udp)
    {
        uv_getaddrinfo_udp_close(req);
    }

    if (req->tcp)
    {
        uv_getaddrinfo_tcp_close(req);
    }

    q = QUEUE_HEAD(&req->dns_server_queue);
    QUEUE_REMOVE(q);
    QUEUE_INSERT_TAIL(&req->dns_server_queue, q);
    s = QUEUE_DATA(q, uv__dnsserver_t, dns_server_queue);

    switch (s->status)
    {
    case 0:
        s->status++;

        uv__getaddrinfo_udp_query(req, s);

        break;
    case 1:
        s->status++;

        uv__getaddrinfo_tcp_query(req, s);

        break;
    default:

        uv__getaddrinfo_end(req, status);

        break;
    }
}

static void uv__getaddrinfo_by_dns(uv_getaddrinfo_t* req)
{

    int ec = uv__get_nameservers(req);

    if (ec == 0)
    {

        uv__getaddrinfo_dns_query_next(req, UV_EAI_NONAME);

    }
    else
    {

        uv__getaddrinfo_end(req, ec);

    }
}

static void uv__getaddrinfo_by_hosts(uv_getaddrinfo_t* req)
{

    req->res = uv__getaddrinfo_file_lookup(req->hostname, req->port);

    if (req->res)
    {

        uv__getaddrinfo_end(req, 0);

    }
    else
    {

        uv__getaddrinfo_by_dns(req);

    }
}

void uv_getaddrinfo(uv_loop_t* loop, uv_getaddrinfo_t* req, const char* hostname, int port, uv_getaddrinfo_cb cb,
                    void* user_data)
{
    assert(req && cb && hostname);

    memset(req, 0, sizeof(*req));

    uv__req_init(loop, req, UV_GETADDRINFO);
    QUEUE_INIT(&req->dns_server_queue);

    req->loop = loop;
    req->cb = cb;
    req->res = null;
    req->port = port;
    req->retcode = 0;
    req->data = user_data;
    req->hostname = strdup(hostname);
    req->id = 0;

    uv_timer_init(req->loop, &req->timer);

    uv__getaddrinfo_by_hosts(req);

}

void uv_getaddrinfo_cancel(uv_getaddrinfo_t* req)
{
    if (req->hostname)
    {
        free(req->hostname);
    }

    uv__dns_free_servers(req);
    uv_timer_stop(&req->timer);
    uv_close((uv_handle_t*) &req->timer);

    uv__req_unregister(req->loop, req);

    if (req->udp)
    {
        uv_getaddrinfo_udp_close(req);
    }

    if (req->tcp)
    {
        uv_getaddrinfo_tcp_close(req);
    }
}
