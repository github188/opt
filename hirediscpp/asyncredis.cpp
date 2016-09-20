/*
 * asyncredis.cpp
 *
 *  Created on: 2016-8-15
 *  Author: cong.li
 */
#include "common/base/base.hpp"
#include "common/avtime/avtime.hpp"

#include "asyncredis.hpp"
#include "common/json/cjson.hpp"
#include <adapters/libuv.h>

using namespace std;
using namespace common;

asyncredis *gpredis = NULL;

asyncredis *asyncredis::instance()
{
    if(gpredis == NULL)
    {
        gpredis = new asyncredis();
    }

    return gpredis;
}

asyncredis::asyncredis() :
    m_hostname("127.0.0.1"),
    m_password("admin"),
    m_port(6379),
    m_timeout(1500),
    m_async_publisher(NULL),
    m_async_subscriber(NULL)
{
    init_config();
    init_redis();
}

asyncredis::~asyncredis() 
{
    if(m_async_publisher)
    {
        redisAsyncDisconnect(m_async_publisher);
    }
    if(m_async_subscriber)
    {
        redisAsyncDisconnect(m_async_subscriber);
    }
}

void asyncredis::push_cmd(const common::string &cmd)
{
    if(m_async_publisher)
    redisAsyncCommand(m_async_publisher, asyncredis::on_redis_ack, this, cmd.c_str());
}


void asyncredis::subscribe_channel(const common::string & channel)
{
    common::string sub;
    sub.append("SUBSCRIBE ");
    sub.append(channel);

    if(m_async_subscriber)
    {
        redisAsyncCommand(m_async_subscriber, asyncredis::on_redis_ack, this, sub.c_str());
    }
    else
    {
        HMLOG_EROR("Subscriber not connected yet!\n");
    }
}

void asyncredis::psubscribe_channel(const common::string & channel)
{
    common::string psub;
    psub.append("PSUBSCRIBE ");
    psub.append(channel);
    
    if(m_async_subscriber)
    {
        redisAsyncCommand(m_async_subscriber, asyncredis::on_redis_ack, this, psub.c_str());
    }
    else
    {
        HMLOG_EROR("Publisher not connected yet!\n");
    }
}

bool asyncredis::json_assert(const common::string &str)
{
    bool is_json = false;
    common::cJSON *root=common::cJSON_Parse(str.c_str());
    if(root != NULL)
    {
        is_json = true;
    }
    common::cJSON_Delete(root);
    return is_json;
}

void asyncredis::on_string(const common::string &s)
{
    HMLOG_IMPT("REDIS_REPLY_STRING(json:%d):%s\n", json_assert(s), s.c_str());
}

void asyncredis::on_array(const std::vector<common::string>& array)
{
    HMLOG_IMPT("REDIS_REPLY_ARRAY:\n");
    for(unsigned int i = 0; i< array.size(); i++)
    {
        HMLOG_IMPT("[json:%d]%04d):%s\n", json_assert(array[i]),i, array[i].c_str()); 
    }
    if(array[0] == "message")
    {
        if(array[1] == "SYSTEM")
        {
            on_string(array[2]);
            redisAsyncCommand(m_async_publisher, 
                asyncredis::on_redis_ack, gpredis, 
                array[2].c_str());
        }
    }
    else if(array[0] == "pmessage")
    {
    }
    else
    {
    }
}

void asyncredis::on_intvalue(long long &value)
{
    HMLOG_IMPT("REDIS_REPLY_INTEGER:%lld\n", value);
}

uint32_t asyncredis::parse_redis_ack(redisReply *reply)
{
    std::vector<common::string> array;

    switch( reply->type )
    {
        case REDIS_REPLY_STRING:
        {
            common::string s(reply->str);
            on_string(s);
            break;
        }
        case REDIS_REPLY_ARRAY:
            for (size_t i = 0; i < reply->elements; i++) 
            {
                if(reply->element[i]->len)
                {
                    common::string s(reply->element[i]->str);
                    array.push_back(s);
                }
            }
            on_array(array);
            break;
        case REDIS_REPLY_NIL:
            HMLOG_IMPT("REDIS_REPLY_NIL\n");
            break;
        case REDIS_REPLY_INTEGER:
            on_intvalue(reply->integer);
            break;
        case REDIS_REPLY_STATUS:
            HMLOG_IMPT("REDIS_REPLY_STATUS: %s\n", reply->str);
            break;
        case REDIS_REPLY_ERROR:
            HMLOG_IMPT("REDIS_REPLY_ERROR: %s\n", reply->str);
            break;
    } 
    return 0;
}

void asyncredis::init_config()
{
    common::xml_document& sysxml = core_config_system();

    m_hostname = sysxml.select("config/redis").attr("hostname");
    m_port = sysxml.select("config/redis").attr("port").to_int32();
    m_timeout = sysxml.select("config/redis").attr("timeout").to_int32();
    m_password = sysxml.select("config/redis").attr("password");
    
    if(!m_hostname) 
        m_hostname = "127.0.0.1";
    if(!m_password) 
        m_password = "admin";
    if(!m_port) 
        m_port = 6379;
    if(!m_timeout)
        m_timeout = 15000;
    HMLOG_INFO("REDIS[%s:%d](%s)\n", m_hostname.c_str(), m_port, m_password.c_str());
}

bool asyncredis::init_redis()
{
    m_async_publisher = redisAsyncConnect(m_hostname.c_str(), m_port);
    if (m_async_publisher == NULL)
    {
        HMLOG_EROR("Init %s:%u error: NULL return\n", m_hostname.c_str(), m_port);
        return false;
    }
    if (m_async_publisher->err) 
    {
        HMLOG_EROR("Init %s:%u error: %s\n", 
            m_hostname.c_str(), m_port, m_async_publisher->errstr);
        redisAsyncFree(m_async_publisher);
        return false;
    }
    m_async_publisher->data = this;
    redisLibuvAttach(m_async_publisher, core_loop()->raw());
    redisAsyncSetConnectCallback(m_async_publisher, asyncredis::on_pub_connect);
    redisAsyncSetDisconnectCallback(m_async_publisher, asyncredis::on_disconnect);

    m_async_subscriber = redisAsyncConnect(m_hostname.c_str(), m_port);
    if (m_async_subscriber == NULL)
    {
        HMLOG_EROR("Init %s:%u error: NULL return\n", m_hostname.c_str(), m_port);
        return false;
    }
    if (m_async_subscriber->err) 
    {
        HMLOG_EROR("Init %s:%u error: %s\n", 
            m_hostname.c_str(), m_port, m_async_subscriber->errstr);
        redisAsyncFree(m_async_subscriber);
        return false;
    }
    m_async_subscriber->data = this;
    redisLibuvAttach(m_async_subscriber, core_loop()->raw());
    redisAsyncSetConnectCallback(m_async_subscriber, asyncredis::on_sub_connect);
    redisAsyncSetDisconnectCallback(m_async_subscriber, asyncredis::on_disconnect);

    return true;
}

void asyncredis::on_redis_ack(redisAsyncContext *c, void *r, void *privdata)
{
    asyncredis* credis = ( asyncredis* )c->data;
    redisReply *reply = (redisReply *)r;
    if (reply == NULL) 
        return;
    credis->parse_redis_ack(reply);
}

void asyncredis::on_pub_connect(const redisAsyncContext *c, int status)
{
    if (status != REDIS_OK) {
        HMLOG_EROR("Connect error: %s\n", c->errstr);
        return;
    }

    if(c->data)
    {
        asyncredis* credis = ( asyncredis* )c->data;
        redisAsyncCommand(const_cast<redisAsyncContext*>(c), 
            asyncredis::on_redis_ack, gpredis, 
            "AUTH %s", 
            credis->get_password().c_str());
    }
}

void asyncredis::on_sub_connect(const redisAsyncContext *c, int status)
{
    if (status != REDIS_OK) {
        HMLOG_EROR("Connect error: %s\n", c->errstr);
        return;
    }
    
    if(c->data)
    {
        asyncredis* credis = ( asyncredis* )c->data;
        redisAsyncCommand(const_cast<redisAsyncContext*>(c), 
            asyncredis::on_redis_ack, gpredis, 
            "AUTH %s", 
            credis->get_password().c_str());
        
        common::xml_document& system = core_config_system();
        size_t num = system.select("config/redis").element_child_number("subscribe");
        for (size_t i = 0; i < num; i++) 
        {
            common::string channel;
            common::xml_node& xml = system.select("config/redis/subscribe[%zd]", i);
            channel = xml.value();
            HMLOG_INFO("Subscribe %s\n", channel.c_str());
            credis->subscribe_channel(channel);
        }
    }
}

void asyncredis::on_disconnect(const redisAsyncContext *c, int status)
{
    if (status != REDIS_OK) {
        HMLOG_EROR("Disconnect error: %s\n", c->errstr);
        redisAsyncFree(const_cast<redisAsyncContext *>(c));
        return;
    }
    HMLOG_WARN("Disconnected...\n");
}


