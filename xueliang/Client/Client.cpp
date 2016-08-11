#include <string>
#include <iostream>
#include <boost/asio.hpp>

#include "Client.hpp"

Client::Client(boost::asio::io_service & ioservice) : 
    m_ioservice(ioservice),
    m_sock(m_ioservice)
{
    
}

Client::~Client()
{
}

void Client::connect(const std::string addr, unsigned short port)
{
    boost::asio::ip::tcp::endpoint endpoint(
        boost::asio::ip::address::from_string(addr), port);

    m_sock.async_connect(endpoint, boost::bind(&Client::onConnect, this, _1));
}

void Client::disconnect()
{
    boost::system::error_code ignored_ec;

    m_sock.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
    m_sock.close(ignored_ec);
}

void Client::onConnect(const boost::system::error_code& error)
{
    if(!error)
    {
        std::cout << "connect ok" << std::endl;
    }
    else
    {
        std::cerr << "Connect error: " << error.value() << std::endl;
    }
}


