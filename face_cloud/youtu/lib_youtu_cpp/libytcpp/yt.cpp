#include "yt.h"
#include <string>

int post_method(const char *addr, const std::string &req_str, std::string &rsp_str)
{
    char *ack = NULL;
    int acklen = 0;
    int err = post_cb_request(addr, req_str.c_str(), ack, &acklen);
    if(err || !ack)
    {
        return err;
    }
    rsp_str.append(ack);
    post_cb_free_ack(ack);
    return 0;
}

// Package Functions
