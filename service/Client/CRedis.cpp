#include <string>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

#include "Client.hpp"
#include "ChannelTemplate.hpp"
#include "CRedis.hpp"
#include "Notify.hpp"
#include "Utils.hpp"

CRedis::CRedis(boost::asio::io_service & ioservice, std::string password) :
    m_ioservice(ioservice), m_pPub(nullptr), m_pSub(nullptr), m_password(password)
{
    m_pPub = new RedisAsyncClient(m_ioservice);
    m_pSub = new RedisAsyncClient(m_ioservice);
}

CRedis::~CRedis()
{
    delete m_pPub;
    delete m_pSub;
}

void CRedis::asyncConnect(const BOOSTADDR &address, 
    unsigned short port, const std::string pass)
{
    m_pPub->asyncConnect(address, port, [&](bool status, const std::string &err)
    {
        if(!status)
        {
            std::cerr << "Can't connect to redis for PUBLISH: " << err << std::endl;
        }
        else
        {
            std::cout << "Pub connect: " << err << std::endl;
        }
    });
    
    m_pSub->asyncConnect(address, port, [&](bool status, const std::string &err)
    {
        if( !status )
        {
            std::cerr << "Can't connect to redis: " << err << std::endl;
        }
        else
        {
            std::cout << "Sub connect: " << err << std::endl;
            /// Regist all channels
            CRegistChannels SubChannels;
            registChannels(SubChannels.channels);
        }
    });

}

void CRedis::connect()
{
    connect("127.0.0.1", 6379);
}

void CRedis::connect(const std::string addr, unsigned short port)
{
    BOOSTADDR address = BOOSTADDR::from_string(addr);
    
    asyncConnect(address, port);
}

void CRedis::disconnect()
{
    m_pPub->disconnect();
    
    CRegistChannels SubChannels;
    unregistChannels(SubChannels.channels);
    m_pSub->disconnect();
}

void CRedis::registChannels(const std::vector<std::string> &channels)
{
    BOOST_FOREACH(std::string channel, channels)
    {
        std::cout << "Regist: " << channel << std::endl;
        m_pSub->subscribe(channel, 
            boost::bind(&CRedis::onMessage, this, _1, _2),
            boost::bind(&CRedis::onSubAck, this, _1));
    }
}

void CRedis::registChannel(const std::string channel)
{
    std::cout << "Regist: " << channel << std::endl;
    m_pSub->subscribe(channel, 
        boost::bind(&CRedis::onMessage, this, _1, _2),
        boost::bind(&CRedis::onSubAck, this, _1));
}

void CRedis::unregistChannels(const std::vector<std::string> &channels)
{
    BOOST_FOREACH(std::string channel, channels)
    {
        std::cout << "UnRegist: " << channel << std::endl;
        RedisAsyncClient::Handle handle = { 0 };
        handle.channel = channel;
        m_pSub->unsubscribe(handle);
    }
}

void CRedis::unregistChannel(const std::string channel)
{
    std::cout << "UnRegist: " << channel << std::endl;
    RedisAsyncClient::Handle handle = { 0 };
    handle.channel = channel;
    m_pSub->unsubscribe(handle);
}

void CRedis::AuthPub()
{
    // Auth
    m_pPub->command(std::string("AUTH"), RedisBuffer(m_password), [&](const RedisValue &v) {
        std::cerr << "Pub auth to server password(" << m_password << "): " << 
            v.toString() << std::endl;
        if(v.toString().compare("OK"))
        {
            // Set connection name
            m_pPub->command(std::string("CLIENT"), RedisBuffer("SETNAME"), 
                RedisBuffer(PUB_CONNECT_NAME), [&](const RedisValue &v) {
                std::cerr << "Pub set connection name(" << PUB_CONNECT_NAME << "): " << 
                    v.toString() << std::endl;
            });
        }
    });
}

void CRedis::AuthSub()
{
    // Set connection name
    m_pSub->command(std::string("CLIENT"), RedisBuffer("SETNAME"), 
        RedisBuffer(SUB_CONNECT_NAME), [&](const RedisValue &v) {
        std::cerr << "Sub set connection name(" << SUB_CONNECT_NAME << "): " 
                  << v.toString() << std::endl;
    });
    // Auth
    m_pSub->command(std::string("AUTH"), RedisBuffer(m_password), [&](const RedisValue &v) {
        std::cerr << "Sub auth to server password(" << m_password << "): " 
                  << v.toString() << std::endl;
    });
}

void CRedis::pushMessage(const std::string &channel, const std::string &message)
{
    if(m_pPub->isConnected())
    {
        m_pPub->publish(channel, message, [&](const RedisValue &) {});
    }
    else
    {
        std::cerr << "Pub client not connected." << std::endl;
    }
}

// Receive subscribed channel message
void CRedis::onMessage(const std::vector<char> &buf, const std::string channel)
{
    std::string msg(buf.begin(), buf.end());

    std::cerr << "[" << channel << "]Message: " << msg << std::endl;

    ///TODO: Handle message
    CNotify notifier;
    notifier.Parse(msg);
    notifier.Dump();

    // Debug mode
    // TEST redis protocl command.
    // Use redis-cli to publish cmd, such as follows:
    // 127.0.0.1:6379> PUBLISH TEST "LRANGE A 1 3"
    // 127.0.0.1:6379> PUBLISH CTRL "stop"
    if(channel == "CTRL" && msg == "stop")
    {
        m_ioservice.stop();
        disconnect();
    }
    else if(channel == "TEST")
    {
        std::vector<std::string> cmd;
        boost::split(cmd, msg, boost::is_any_of(" "), boost::token_compress_on);
        std::list<RedisBuffer> args;
        for(unsigned int c = 1; c < cmd.size(); c++)
        {
            args.push_back(cmd[c]);
        }
        if(cmd[0] == "LRANGE")
            m_pPub->command(cmd[0], args, boost::bind(&CRedis::onList, this, _1));
        else
            m_pPub->command(cmd[0], args, boost::bind(&CRedis::onCommonAck, this, _1));
            
    }
}

// Called every channed subscribed
void CRedis::onSubAck(const RedisValue &value)
{
    IMPT("Sub ACK: \n");
    std::cout << value.inspect() << std::endl;
}

void CRedis::onList(const RedisValue &value)
{
    IMPT("LRANGE ACK: \n");
    std::cout << value.inspect() << std::endl;
}

void CRedis::onCommonAck(const RedisValue &value)
{
    IMPT("Command ACK: \n");
    std::cout << value.inspect() << std::endl;
}


