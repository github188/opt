/**
 * @file   http_get.h
 * @author Wang Hong <hong.wang@huamaitel.com>
 * @date   2015-08-06
 * 
 * @brief  http client
 * 
 */
#ifndef __HTTP_GET_H__
#define __HTTP_GET_H__

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

int http_get(char *url,struct iovec *buf);
int http_post(const char *url,
    const char *content,size_t content_len,
    const char *content_type,struct iovec *response, 
    const char *auth);
#endif
