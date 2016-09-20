/*
 * asyncredis.hpp
 *
 *  Created on: 2016-8-12
 *     Author: cong.li
 */

#ifndef ASYNC_REDIS_HPP
#define ASYNC_REDIS_HPP

#include <queue>
#include <vector>
#include "moduleid.hpp"
#include "messageid.hpp"
#include "core_api.hpp"
#include "common/uvcpp/uvloop.hpp"

#include <hiredis.h>
#include <async.h>

class asyncredis
{
public:
    static asyncredis *instance();
    void push_cmd(const common::string &cmd);
    ~asyncredis();
    
protected:
    asyncredis();

    static void on_redis_ack(redisAsyncContext *c, void *r, void *privdata);
    static void on_pub_connect(const redisAsyncContext *c, int status);
    static void on_sub_connect(const redisAsyncContext *c, int status);
    static void on_disconnect(const redisAsyncContext *c, int status);
    
    void init_config();
    bool init_redis();
    void subscribe_channel(const common::string & channel);
    void psubscribe_channel(const common::string & channel);
    bool json_assert(const common::string &str);
    void on_string(const common::string &s);
    void on_array(const std::vector<common::string>& array);
    void on_intvalue(long long &value);
    uint32_t parse_redis_ack(redisReply *reply);
    common::string get_password() { return m_password; };
    
private:
    common::string m_hostname;
    common::string m_password;
    int m_port;
    uint16_t m_timeout;
    redisAsyncContext *m_async_publisher;
    redisAsyncContext *m_async_subscriber;
};

#endif /* ASYNC_REDIS_HPP */


