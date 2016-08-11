#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <boost/asio.hpp>
#include <boost/array.hpp>

struct hm_head 
{
    unsigned int cmd;
    unsigned int size;
    unsigned int error;
    unsigned int session;
};

struct hm_message
{
    hm_head head;
    char *body;
};

#endif /* PROTOCOL_HPP */


