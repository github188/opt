#ifndef SYNC_REDIS_HPP
#define SYNC_REDIS_HPP

#include <vector>
#include <sstream>
#include "core_api_config.hpp"

#include "hiredis.h"

struct lpos
{
    std::string tostring()
    {
        std::string str;
        std::stringstream ss;
        ss << pos;
        str = ss.str();
        return str;
    };
    long long pos;
};

class syncredis
{
public:
    syncredis();
    ~syncredis();
    common::string HGET(const common::string &key, const common::string &field);
    bool HEXISTS(const common::string &key, const common::string &field);
    std::vector<common::string> LRANGE(const common::string &key, lpos start, lpos end);
protected:
    void init_config();
    bool connect();
    bool disconnect();
    bool json_assert(const common::string &str);
    bool parse(redisReply *reply, std::vector<common::string> &array);
private:
    common::string m_hostname;
    common::string m_password;
    int m_port;
    uint16_t m_timeout;
    bool m_connected;
    redisContext *m_c;
};

#endif /* SYNC_REDIS_HPP */

