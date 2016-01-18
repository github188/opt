
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "http_parser.h"
#include "stt.h"
#include "net.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define HTTP_PARSER_BUF_SIZE 1024
#define HTTP_BODY_SAFE_BUFSIZE 16 
#define MAX_HTTP_BODY 4096
#define HTTP_CONNECT_TIMEOUT 5000
#define HTTP_SEND_TIMEOUT 10

int on_headers_complete(http_parser *parser)
{
    struct iovec *buf=NULL;
    if(parser->content_length == (uint64_t)-1)
    {
        printf(" Transfer-Encoding: chunked is not supported\n");
        return -1;
    }
    buf = (struct iovec *)parser->data;
    if(buf){
        if(parser->content_length > MAX_HTTP_BODY)
        {
            return -1;
        }
        if(parser->content_length >0)
        {
            buf->iov_base= malloc(parser->content_length + HTTP_BODY_SAFE_BUFSIZE);
            if (buf->iov_base == NULL)
                return -1;
            memset(buf->iov_base, 0x0, parser->content_length + HTTP_BODY_SAFE_BUFSIZE);
        }
    }
    return 0;
}

int on_body(http_parser* parser, const char *at, size_t length)
{
    struct iovec *buf=NULL;
    buf = (struct iovec *)parser->data;
    if(buf){
        if(buf->iov_len+length>MAX_HTTP_BODY)
        {
            printf("on_body recved = %d > MAX_HTTP_BODY = %d\n",buf->iov_len+length,MAX_HTTP_BODY);
            return -1;
        }
        memcpy((char *)buf->iov_base + buf->iov_len,at,length);
        buf->iov_len+=length;
    }
    return 0;
}

int http_post(const char *url,const char *content,size_t content_len,const char *content_type,struct iovec *response, const char *auth){
    http_parser parser;
    st_netfd_t fd=NULL;
    size_t  nparsed=0;
    char rbuf[HTTP_PARSER_BUF_SIZE] = "";
    char *host=NULL;
    size_t len=0;
    ssize_t r=0;
    struct sockaddr_in srv;
    struct http_parser_url p;
    memset(&p,0,sizeof(p));
    memset(response,0,sizeof(struct  iovec));
    memset(&srv, 0, sizeof(struct sockaddr_in));
    http_parser_settings settings;
    settings.on_message_begin=NULL;
    settings.on_url=NULL;
    settings.on_header_field=NULL;
    settings.on_header_value=NULL;
    settings.on_headers_complete=on_headers_complete;
    settings.on_body=on_body;
    settings.on_message_complete=NULL;
    
    if(http_parser_parse_url(url,strlen(url),0,&p)!=0)
    {   
        printf("http_parser_parse_url fail\n");
        return -1;
    }
    host=(char *)malloc(p.field_data[UF_HOST].len+1);
    snprintf(host,p.field_data[UF_HOST].len+1,"%.*s",p.field_data[UF_HOST].len,p.field_data[UF_HOST].off+url);
    len=snprintf(rbuf, sizeof(rbuf),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Connection: Close\r\n"
        "Content-Type:%s\r\n"
        "Content-Length:%u\r\n"
        "User-Agent: LINUX, HM http_POST\r\n"
        "Expect: \r\n"
        "Authorization: %s\r\n"
        "\r\n",
        url+p.field_data[UF_PATH].off,
        host,
        p.port==0? 80:p.port,
        content_type,(uint32_t)content_len, auth);

    fd = net_connect(host,p.port>0? p.port:80,HTTP_CONNECT_TIMEOUT);
    if (fd == NULL){
        goto end;
    }
    if(st_write(fd, rbuf, len, HTTP_SEND_TIMEOUT)==-1){
        goto end;
    }
    if(st_write(fd, content, content_len, HTTP_SEND_TIMEOUT)==-1){
        goto end;
    }
    http_parser_init(&parser, HTTP_RESPONSE);
    parser.data=response;
    while( (r=st_read(fd,rbuf,HTTP_PARSER_BUF_SIZE,HTTP_SEND_TIMEOUT))>0)
    {
        if(r<0)goto end;
        nparsed=http_parser_execute(&parser, &settings, rbuf, r);       
        if((size_t)r != nparsed)
        {
            goto end;
        }       
    }   
    st_netfd_close(fd);
    if(host)free(host);
    return parser.status_code;
    
end:
    if(fd)st_netfd_close(fd);
    if(host)free(host);
    if(response->iov_base)free(response->iov_base);
    response->iov_base=NULL;
    response->iov_len=0;
    return -1;
}


int http_get(char *url,struct iovec *buf){
    http_parser parser;
    st_netfd_t fd=NULL;
    size_t  nparsed=0;
    char rbuf[HTTP_PARSER_BUF_SIZE] = "";
    char *host=NULL;
    size_t len=0;
    ssize_t r=0;
    struct sockaddr_in srv;
    struct http_parser_url p;
    memset(&p,0,sizeof(p));
    memset(buf,0,sizeof(struct  iovec));
    memset(&srv, 0, sizeof(struct sockaddr_in));
    http_parser_settings settings;
    settings.on_message_begin=NULL;
    settings.on_url=NULL;
    settings.on_header_field=NULL;
    settings.on_header_value=NULL;
    settings.on_headers_complete=on_headers_complete;
    settings.on_body=on_body;
    settings.on_message_complete=NULL;

    if(http_parser_parse_url(url,strlen(url),0,&p)!=0)
    {   
        printf("http_parser_parse_url fail\n");
        return -1;
    }
    host=(char *)malloc(p.field_data[UF_HOST].len+1);
    snprintf(host,p.field_data[UF_HOST].len+1,"%.*s",p.field_data[UF_HOST].len,p.field_data[UF_HOST].off+url);
    len=snprintf(rbuf, sizeof(rbuf),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Connection: Close\r\n"
        "User-Agent: LINUX, HM http_get\r\n"
        "\r\n",
        url+p.field_data[UF_PATH].off,
        host,
        p.port==0? 80:p.port);

    fd = net_connect(host,p.port>0? p.port:80,HTTP_CONNECT_TIMEOUT);
    if (fd == NULL){
        goto end;
    }

    if(st_write(fd, rbuf, len, HTTP_SEND_TIMEOUT)==-1){
        goto end;
    }
    
    http_parser_init(&parser, HTTP_RESPONSE);
    parser.data=buf;
    while( (r=st_read(fd,rbuf,HTTP_PARSER_BUF_SIZE,HTTP_SEND_TIMEOUT))>0)
    {
        if(r<0)goto end;
        nparsed=http_parser_execute(&parser, &settings, rbuf, r);       
        if((size_t)r != nparsed)
        {
            goto end;
        }       
    }   
    st_netfd_close(fd);
    if(host)free(host);
    return parser.status_code;
    
end:
    if(fd)st_netfd_close(fd);
    if(host)free(host);
    if(buf->iov_base)free(buf->iov_base);
    buf->iov_base=NULL;
    buf->iov_len=0;
    return -1;
}
