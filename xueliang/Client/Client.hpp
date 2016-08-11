#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>

typedef boost::asio::ip::address BOOSTADDR;

class Client : public boost::enable_shared_from_this<Client>,
    private boost::noncopyable
{
public:
    explicit Client(boost::asio::io_service & ioservice);
    ~Client();
    void connect(const std::string addr, unsigned short port);
    void disconnect();
private:
    void onConnect(const boost::system::error_code& error);
protected:
    boost::asio::io_service &m_ioservice;
    boost::asio::ip::tcp::socket m_sock;
};


#endif /* CLIENT_HPP */


