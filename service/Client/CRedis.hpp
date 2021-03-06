#include <string>
#include <iostream>
#include <vector>

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>

#include "redisasyncclient.h"

#define PUB_CONNECT_NAME "service.system.publisher"
#define SUB_CONNECT_NAME "service.system.subscriber"

typedef boost::asio::ip::address BOOSTADDR;

class CRedis
{
public:
    explicit CRedis(boost::asio::io_service & ioservice, std::string password);
    
    ~CRedis();

    // Default redis server
    void connect();
    
    void connect(const std::string addr, unsigned short port);
    
    void disconnect();

    void pushMessage(const std::string &channel, const std::string &message);

    void registChannels(const std::vector<std::string> &channels);

    void registChannel(const std::string channel);

    void unregistChannels(const std::vector<std::string> &channels);

    void unregistChannel(const std::string channel);
    
protected:

    void asyncConnect(const BOOSTADDR &address, unsigned short port, 
        const std::string pass = "admin");

    void onMessage(const std::vector<char> &buf, const std::string channel);
    
    void onSubAck(const RedisValue &value);

    void AuthPub();

    void AuthSub();

    void onList(const RedisValue &value);

    void onCommonAck(const RedisValue &value);

private:

    boost::asio::io_service &m_ioservice;

    RedisAsyncClient *m_pPub;
    
    RedisAsyncClient *m_pSub;

    std::string m_password;
};

