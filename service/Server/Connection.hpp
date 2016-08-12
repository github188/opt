#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/logic/tribool.hpp>
#include <string>
#include <list>
#include <sstream>

#include "RequestHandler.hpp"
#include "Protocol.hpp"

namespace service {

enum ReadStatus
{
    READ_HEAD,
    READ_BODY
};

struct connection_attr
{
    std::string id;    /* Init once, read only */
    std::string ip;
    unsigned short port;
    boost::posix_time::ptime last_t;
    connection_attr()
    {
        
    }

    connection_attr &operator=(connection_attr &o)
    {
        this->ip = o.ip;
        this->last_t = o.last_t;
        this->port = o.port;
        return *this;
    }
    connection_attr &operator=(const connection_attr &o)
    {
        this->ip = o.ip;
        this->last_t = o.last_t;
        this->port = o.port;
        return *this;
    }
    bool operator==(connection_attr &o)
    {
        if(this->id == o.id)
            return true;
        else
            return false;
    }
    bool operator==(const connection_attr &o)
    {
        if(this->id == o.id)
            return true;
        else
            return false;
    }
};

/// Represents a single connection from a client.
class connection
    :   public boost::enable_shared_from_this<connection>,
        private boost::noncopyable
{
public:
    /// Construct a connection with the given io_service.
    explicit connection(boost::asio::io_service& io_service,
        request_handler& handler, 
        std::list<connection_attr> &connect_attr_list);

    ~connection();

    /// Get the socket associated with the connection.
    boost::asio::ip::tcp::socket& socket();

    /// Start the first asynchronous operation for the connection.
    void start();

private:
    /// Handle completion of a read operation.
    void handle_read(const boost::system::error_code& e,
        std::size_t bytes_transferred);

    /// Handle completion of a write operation.
    void handle_write(const boost::system::error_code& e);

    /// Strand to ensure the connection's handlers are not called concurrently.
    boost::asio::io_service::strand m_strand;

    /// Socket for the connection.
    boost::asio::ip::tcp::socket m_socket;

    /// The handler used to process the incoming request.
    request_handler& m_request_handler;

    /// The incoming request.
    hm_message m_request;

    /// The reply to be sent back to the client.
    hm_message m_reply;

    /// Read process status
    ReadStatus m_reading;

    connection_attr m_attr;

    std::list<connection_attr> &m_connect_attr_list;
};

typedef boost::shared_ptr<connection> connection_ptr;
typedef std::list<connection_attr> connect_attr_list;

} // service

#endif // CONNECTION_HPP

