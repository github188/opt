
#include <vector>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/logic/tribool.hpp>
#include <boost/tuple/tuple.hpp>

#include "RequestHandler.hpp"
#include "Connection.hpp"

namespace xl {

connection::connection(boost::asio::io_service& io_service,
    request_handler& handler) : 
    // Member initialization
    m_strand(io_service),
    m_socket(io_service),
    m_request_handler(handler),
    m_request(),
    m_reply(),
    m_reading(READ_HEAD)
{
}

boost::asio::ip::tcp::socket& connection::socket()
{
    return m_socket;
}

void connection::start()
{
    m_reading = READ_HEAD;
    m_socket.async_read_some(
        boost::asio::buffer(&m_request.head, sizeof(m_request.head)),
        m_strand.wrap(boost::bind(&connection::handle_read, shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred)));
}

void connection::handle_read(const boost::system::error_code& e,
    std::size_t bytes_transferred)
{
    if (!e)
    {
        switch(m_reading)
        {
            case READ_HEAD:
                m_reading = READ_BODY;
                std::cout << "MessageID:" << std::hex << m_request.head.cmd << std::dec << std::endl;
                m_request.body = new char[m_request.head.size];
                m_socket.async_read_some(
                    boost::asio::buffer(m_request.body, m_request.head.size),
                    m_strand.wrap(boost::bind(&connection::handle_read, shared_from_this(),
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred)));
                break;
            case READ_BODY:
                m_reading = READ_HEAD;
                std::cout << "Read size=" << bytes_transferred << std::endl;

                /// TODO: Handle request
                m_request_handler.handle_request(m_request, m_reply);
                delete [] m_request.body;
                m_socket.async_read_some(
                    boost::asio::buffer(&m_request.head, sizeof(m_request.head)),
                    m_strand.wrap(boost::bind(&connection::handle_read, shared_from_this(),
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred)));
                break;
        }
        
    }

    // If an error occurs then no new asynchronous operations are started. This
    // means that all shared_ptr references to the connection object will
    // disappear and the object will be destroyed automatically after this
    // handler returns. The connection class's destructor closes the socket.
}

void connection::handle_write(const boost::system::error_code& e)
{
    if (!e)
    {
        // Initiate graceful connection closure.
        boost::system::error_code ignored_ec;
        m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
    }

    // No new asynchronous operations are started. This means that all shared_ptr
    // references to the connection object will disappear and the object will be
    // destroyed automatically after this handler returns. The connection class's
    // destructor closes the socket.
}

} // xl

