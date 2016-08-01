/**
 * @file   http_req.c
 * @author Lee <lovelacelee@gmail.com>
 * @date   2015-08-06
 * 
 * @brief  http request sync
 * 
 */
#ifndef __HTTP_REQ_H__
#define __HTTP_REQ_H__

int http_get(char *url, struct iovec *buf);
int http_post(char *url, char *content, size_t content_len, char *content_type, struct iovec *response);
 
#endif
