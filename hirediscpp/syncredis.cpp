#include "common/base/base.hpp"
#include "common/avtime/avtime.hpp"
#include "common/json/cjson.hpp"
#include "syncredis.hpp"

using namespace common;

syncredis::syncredis() :
    m_hostname("127.0.0.1"),
    m_password("admin"),
    m_port(6379),
    m_timeout(1500),
    m_connected(false),
    m_c(NULL)
{
    init_config();
    m_connected = connect();
}

syncredis::~syncredis()
{
    disconnect();
}

common::string syncredis::HGET(const common::string &key, const common::string &field)
{
    common::string cmd;
    redisReply *reply = NULL;
    common::string strret;
    
    cmd.append("HGET ");
    cmd.append(key);
    cmd.append(" ");
    cmd.append(field);
    HMLOG_INFO("%s\n", cmd.c_str());
    if(m_connected)
    {
        redisAppendCommand(m_c, cmd.c_str());
        if(redisGetReply(m_c, (void **)&reply) == REDIS_OK)
        {
            strret.append(reply->str);
            freeReplyObject(reply);
            return strret;
        }
        else
        {
            HMLOG_EROR("replay failed\n");
        }
    }
    else
    {
        HMLOG_EROR("Not connected\n");
    }
    return strret;
}

std::vector<common::string> syncredis::LRANGE(const common::string &key, lpos start, lpos end)
{
    common::string cmd;
    redisReply *reply = NULL;
    std::vector<common::string> array;
    
    cmd.append("LRAGE ");
    cmd.append(key);
    cmd.append(" ");
    cmd.append(start.tostring().c_str());
    cmd.append(" ");
    cmd.append(end.tostring().c_str());
    if(m_connected)
    {
        redisAppendCommand(m_c, cmd.c_str());
        if(redisGetReply(m_c, (void **)&reply) == REDIS_OK)
        {
            parse(reply, array);
            freeReplyObject(reply);
        }
    }
    else
    {
        HMLOG_EROR("Not connected\n");
    }
    return array;
}

bool syncredis::HEXISTS(const common::string &key, const common::string &field)
{
    common::string cmd;
    redisReply *reply = NULL;
    
    cmd.append("HEXISTS ");
    cmd.append(key);
    cmd.append(" ");
    cmd.append(field);
    HMLOG_INFO("%s\n", cmd.c_str());
    if(m_connected)
    {
        redisAppendCommand(m_c, cmd.c_str());
        if(redisGetReply(m_c, (void **)&reply) == REDIS_OK)
        {
            HMLOG_INFO("reply value %lld\n", reply->integer);
            bool ret = (reply->integer == 0) ? false : true;
            freeReplyObject(reply);
            return ret;
        }
        else
        {
            HMLOG_EROR("replay failed\n");
        }
    }
    else
    {
        HMLOG_EROR("Not connected\n");
    }
    return false;
}

// protected member functions 
void syncredis::init_config()
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
    HMLOG_INFO("SYNC REDIS[%s:%d](%s)\n", m_hostname.c_str(), m_port, m_password.c_str());
}
bool syncredis::connect()
{
    redisReply *reply = NULL;
    bool connect_ret = false;
    do
    {
        struct timeval timeout = { 
            m_timeout/1000, /* tv_sec */
            (m_timeout%1000) * 1000 /* tv_usec */
        }; 

        HMLOG_IMPT("connect to redis server [%s:%d]\n", m_hostname.c_str(), m_port);
        m_c = redisConnectWithTimeout(m_hostname.c_str(), m_port, timeout);
        if(m_c == NULL)
        {
            connect_ret = false;
            HMLOG_EROR("Damn connect failed!\n");
            break;
        }

        if (m_c->err) 
        {
            HMLOG_EROR("Connection error: %s\n", m_c->errstr);    
            redisFree(m_c);
            connect_ret = false;
            break;
        }
                  
        reply = (redisReply *)redisCommand(m_c, "AUTH %s", m_password.c_str());
        if(reply)
        {
            HMLOG_INFO("%s\n", reply->str);
            freeReplyObject(reply);
            reply=NULL;
        }
        reply = (redisReply *)redisCommand(m_c, "PING");
        if(!strcmp(reply->str, "PONG"))
        {
            connect_ret = true;
            break;
        }
    }while(0);
    
    if(reply)
    {
        freeReplyObject(reply);
        reply=NULL;
    }
    return connect_ret;
}

bool syncredis::disconnect()
{
    if(m_connected)
    {
        redisFree(m_c);
        return true;
    }
    return false;
}


bool syncredis::json_assert(const common::string &str)
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

bool syncredis::parse(redisReply *reply, std::vector<common::string> &array)
{
    if(reply->type == REDIS_REPLY_ARRAY)
    {
        for (size_t i = 0; i < reply->elements; i++) 
        {
            if(reply->element[i]->len)
            {
                common::string s(reply->element[i]->str);
                array.push_back(s);
            }
        }
        return true;
    }
    else
    {
        HMLOG_EROR("Redis acktype [%d != %d]\n", reply->type, REDIS_REPLY_ARRAY);
        return false;
    }
}


