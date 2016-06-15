#ifndef __YT_H__
#define __YT_H__

typedef int (*post_cb_request)(const char *addr, const char *req, char **ack, int *acklen);

typedef void (*post_cb_free_ack)(char *ack);


#endif /* __YT_H__ */

