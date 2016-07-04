/**
 * @file   http_req.c
 * @author Lee <lovelacelee@gmail.com>
 * @date   2015-08-06
 * 
 * @brief  http request sync
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "http_parser.h"
#include "common.h"
#include "stt.h"
#include "net.h"

#define HTTP_PARSER_BUF_SIZE    1024
#define HTTP_GET_MAX_SIZE       4096
#define HTTP_BODY_SAFE_BUFSIZE  16 
#define MAX_HTTP_BODY           4096
#define HTTP_CONNECT_TIMEOUT    5*1000*1000
#define HTTP_SEND_TIMEOUT       5*1000*1000

size_t sstrlen(const char *str)
{
    if (str)
        return strlen(str);
    else
        return 0;
}

int on_headers_complete(http_parser *parser)
{
    struct iovec *buf=NULL;
    if(parser->content_length ==-1)
    {
        log_err(" Transfer-Encoding: chunked is not supported\n");
        return -1;
    }
    buf = (struct iovec *)parser->data;
    if(buf){
        if(parser->content_length > MAX_HTTP_BODY)
        {
            log_err(" content_length %lld > MAX_HTTP_BODY\n",parser->content_length);
            return -1;
        }
        if(parser->content_length >0)
        {
            buf->iov_base= malloc(parser->content_length + HTTP_BODY_SAFE_BUFSIZE);
            if (buf->iov_base == NULL)
                return -1;
             // Must memset before use it, otherwise cause invalid read 
             // or wirte(FATAL ERROR!!!) in caller.
            memset(buf->iov_base, 0x0, parser->content_length + HTTP_BODY_SAFE_BUFSIZE);
            //log_inf("===malloc:%p :%lu iov_len %d\n",buf->iov_base, parser->content_length, buf->iov_len);
        }
    }
    return 0;
}

int on_body(http_parser* parser, const char *at, size_t length)
{
    struct iovec *buf=NULL;
    buf = parser->data;
    if(buf){
        if(buf->iov_len+length>MAX_HTTP_BODY)
        {
            log_err("on_body recved = %d > MAX_HTTP_BODY = %d\n",buf->iov_len+length,MAX_HTTP_BODY);
            return -1;
        }
        //log_inf("=1=iov_len:%d\n", buf->iov_len);
        memcpy(buf->iov_base+buf->iov_len,at,length);
        buf->iov_len+=length;
        //log_inf("=2=iov_len:%d\n", buf->iov_len);
    }
    return 0;
}

int http_post(char *url,char *content,size_t content_len,char *content_type,struct iovec *response){
    http_parser parser;
    st_netfd_t fd=NULL;
    size_t  nparsed=0;
    char rbuf[HTTP_PARSER_BUF_SIZE] = "";
    char *host=NULL;
    size_t len=0;
    ssize_t r=0;
    int ret = -1;
    struct sockaddr_in srv;
    struct http_parser_url p;
    memset(&p,0,sizeof(p));
    memset(response,0,sizeof(struct  iovec));
    memset(&srv, 0, sizeof(struct sockaddr_in));
    http_parser_settings settings={
        .on_message_begin=NULL,
        .on_url=NULL,
        .on_header_field=NULL,
        .on_header_value=NULL,
        .on_headers_complete=on_headers_complete,
        .on_body=on_body,
        .on_message_complete=NULL
    };
    
    if(http_parser_parse_url(url,strlen(url),0,&p)!=0)
    {   
        log_err("http_parser_parse_url fail\n");
        return -1;
    }
    host=(char *)malloc(p.field_data[UF_HOST].len+1);
    snprintf(host,p.field_data[UF_HOST].len+1,"%.*s",p.field_data[UF_HOST].len,p.field_data[UF_HOST].off+url);
    len=snprintf(rbuf, sizeof(rbuf),
        "POST %s HTTP/1.1\r\n"
        "Accept: */*\r\n"
        "Host: %s:%d\r\n"
        "Connection: Close\r\n"
        "Content-Type:%s\r\n"
        "Content-Length:%u\r\n"
        "User-Agent: LINUX, HM http_POST\r\n"
        "\r\n",
        url+p.field_data[UF_PATH].off,
        host,
        p.port==0? 80:p.port,
        content_type,(uint32_t)content_len);

    fd = net_connect(host,p.port>0? p.port:80,HTTP_CONNECT_TIMEOUT);
    if (fd == NULL){
        ret = -2;
        goto end;
    }
    //printf("====================================================\n");
    if(st_write(fd, rbuf, len, HTTP_SEND_TIMEOUT)==-1){
        ret = -3;
        goto end;
    }
    //printf("%.*s",len,rbuf);
    //printf("%.*s",content_len,content);
    if(st_write(fd, content, content_len, HTTP_SEND_TIMEOUT)==-1){
        ret = -4;
        goto end;
    }
    //printf("=========================end=========================\n");
    http_parser_init(&parser, HTTP_RESPONSE);
    parser.data=response;
    while( (r=st_read(fd,rbuf,HTTP_PARSER_BUF_SIZE,HTTP_SEND_TIMEOUT))>0)
    {
        if(r<0)
        {
            ret = -5;
            goto end;
        }
        nparsed=http_parser_execute(&parser, &settings, rbuf, r);       
        if(r != nparsed)
        {
            ret = -6;
            goto end;
        }       
    }
    ret = parser.status_code;
end:
    if(fd)
    {
        st_netfd_close(fd);
        fd = NULL;
    }
    if(host)
    {
        free(host);
        host = NULL;
    }
#if 0 /* Free unuseful */
    if(response->iov_base)
    {
        free(response->iov_base);
        response->iov_base=NULL;
        response->iov_len=0;
    }
#endif /* 0 Free unuseful */
    return ret;
}


int http_get(char *url,struct iovec *buf){
    http_parser parser = { 0 };
    st_netfd_t fd = NULL;
    size_t  nparsed = 0;
    char rbuf[HTTP_PARSER_BUF_SIZE] = "";
    char *request  = NULL;
    char *host = NULL;
    size_t len = 0;
    ssize_t r = 0;
    int ret = -1;
    struct sockaddr_in srv;
    struct http_parser_url p;
    memset(&p,0,sizeof(p));
    memset(buf,0,sizeof(struct  iovec));
    memset(&srv, 0, sizeof(struct sockaddr_in));
    http_parser_settings settings={
        .on_message_begin=NULL,
        .on_url=NULL,
        .on_header_field=NULL,
        .on_header_value=NULL,
        .on_headers_complete=on_headers_complete,
        .on_body=on_body,
        .on_message_complete=NULL
    };

    if(http_parser_parse_url(url,sstrlen(url),0,&p)!=0)
    {   
        log_err("http_parser_parse_url fail\n");
        return -1;
    }
    host=(char *)malloc(p.field_data[UF_HOST].len+1);
    snprintf(host,p.field_data[UF_HOST].len+1,"%.*s",p.field_data[UF_HOST].len,p.field_data[UF_HOST].off+url);
    request = (char *)malloc(HTTP_GET_MAX_SIZE + sstrlen(url));
    len = snprintf(request, HTTP_GET_MAX_SIZE,
        "GET %s HTTP/1.1\r\n"
        "Accept: */*\r\n"
        "Host: %s:%d\r\n"
        "Connection: Close\r\n"
        "User-Agent: LINUX, HM http_get\r\n"
        "\r\n",
        url+p.field_data[UF_PATH].off,
        host,
        p.port==0? 80:p.port);
    log_inf("%s\r\n", request);
    fd = net_connect(host,p.port>0? p.port:80,HTTP_CONNECT_TIMEOUT);
    if (fd == NULL){
        ret = -1;
        goto end;
    }

    if(st_write(fd, request, len, HTTP_SEND_TIMEOUT)==-1){
        ret = -2;
        goto end;
    }
    
    http_parser_init(&parser, HTTP_RESPONSE);
    parser.data=buf;
    while( (r=st_read(fd,rbuf,HTTP_PARSER_BUF_SIZE,HTTP_SEND_TIMEOUT))>0)
    {
        if(r<0)
        {
            ret = -3;
            goto end;
        }
        nparsed=http_parser_execute(&parser, &settings, rbuf, r);       
        if(r != nparsed)
        {
            ret = -4;
            goto end;
        }       
    }   
    ret = parser.status_code;
    
end:
    if(fd)
    {
        st_netfd_close(fd);
        fd = NULL;
    }
    if(host)
    {
        free(host);
        host = NULL;
    }
    if(request)
    {
        free(request);
        request = NULL;
    }
#if 0 /* Free it unuseful */
    if(buf->iov_base)
    {
        free(buf->iov_base);
        buf->iov_base=NULL;
        buf->iov_len=0;
    }
#endif /* 0 Free it unuseful */
    return ret;
}

