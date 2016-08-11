#include <string>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/foreach.hpp>

#include "Client.hpp"
#include "ChannelTemplate.hpp"
#include "CRedis.hpp"
#include "Notify.hpp"

CRedis::CRedis(boost::asio::io_service & ioservice) :
    m_ioservice(ioservice)
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
            // Set connection name
            m_pPub->command("CLIENT", "SETNAME", 
                PUB_CONNECT_NAME, [&](const RedisValue &v) {
                std::cerr << "Pub set connection name(" << PUB_CONNECT_NAME << "): " 
                          << v.toString() << std::endl;
            });
            // Auth
            m_pPub->command("AUTH", pass, [&](const RedisValue &v) {
                std::cerr << "Pub auth to server password(" << pass << "): " 
                          << v.toString() << std::endl;
            });
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
            // Set connection name
            m_pSub->command("CLIENT", "SETNAME", 
                SUB_CONNECT_NAME, [&](const RedisValue &v) {
                std::cerr << "Sub set connection name(" << SUB_CONNECT_NAME << "): " 
                          << v.toString() << std::endl;
            });
            // Auth
            m_pSub->command("AUTH", pass, [&](const RedisValue &v) {
                std::cerr << "Sub auth to server password(" << pass << "): " 
                          << v.toString() << std::endl;
            });
            
            /// Regist all channels
            CRegistChannels SubChannels;
            registChannels(SubChannels.channels);
        }
    });

}

void CRedis::connect()
{
    connect("127.0.0.1", 6379, "admin");
}

void CRedis::connect(const std::string addr, unsigned short port)
{
    BOOSTADDR address = BOOSTADDR::from_string(addr);
    
    asyncConnect(address, port);
}

void CRedis::connect(const std::string addr, unsigned short port, const std::string pass)
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
        RedisAsyncClient::Handle handle;
        handle.channel = channel;
        m_pSub->unsubscribe(handle);
    }
}

void CRedis::unregistChannel(const std::string channel)
{
    std::cout << "UnRegist: " << channel << std::endl;
    RedisAsyncClient::Handle handle;
    handle.channel = channel;
    m_pSub->unsubscribe(handle);
}

void CRedis::pushMessage(const std::string &channel, const std::string &message)
{
    if(m_pPub->isConnected())
    {
        m_pPub->publish(channel, message, [&](const RedisValue &) {
            std::cerr << "Pub channel(" << channel << ") ok:" << std::endl;
            std::cerr << message << std::endl;
        });
    }
    else
    {
        std::cerr << "Pub client not connected." << std::endl;
    }
}

void CRedis::onMessage(const std::vector<char> &buf, const std::string channel)
{
    std::string msg(buf.begin(), buf.end());

    std::cerr << "[" << channel << "]Message: " << msg << std::endl;

    ///TODO: Handle message
    CNotify notifier;
    notifier.Parse(msg);
    notifier.Dump();
}

// Called every channed subscribed
void CRedis::onSubAck(const RedisValue &value)
{
    std::vector<RedisValue> result = value.toArray();
    if(result.size() == 3)
    {
        const RedisValue &command = result[0];
        const RedisValue &queueName = result[1];
        const RedisValue &value = result[2];

        std::cout << command.toString() << "ing "
            << queueName.toString() << " "
            << value.toString() << std::endl;
    }
}


