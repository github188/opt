
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
    m_reading(READ_HEADLEN)
{
}

boost::asio::ip::tcp::socket& connection::socket()
{
    return m_socket;
}

void connection::start()
{
    m_reading = READ_HEADLEN;
    m_socket.async_read_some(
        boost::asio::buffer(&m_request.HeadLen, sizeof(m_request.HeadLen)),
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
            case READ_HEADLEN:
                /// Headlen read ok
                std::cout << "Read size=" << bytes_transferred << " Head size: " << m_request.HeadLen << std::endl;
                m_reading = READ_HEAD;
                std::cout << "HeadStart: " << static_cast<const void *>(&m_request) << std::endl;
                std::cout << "JumpSize : " << static_cast<const void *>(m_request.HeadBody()) << std::endl;
                std::cout << "HeadLeft : " << m_request.HeadBodyLen() << std::endl;
                m_socket.async_read_some(
                    boost::asio::buffer(m_request.HeadBody(), m_request.HeadBodyLen()),
                    m_strand.wrap(boost::bind(&connection::handle_read, shared_from_this(),
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred)));
                break;
            case READ_HEAD:
                m_reading = READ_BODY;
                std::cout << "Read size=" << bytes_transferred << " HeadParsing: " << std::endl;
                std::cout << "MessageID:" << std::hex << m_request.Id << std::dec << std::endl;
                std::cout << "XmlLen:" << m_request.BodyLen << std::endl;
                std::cout << "From:" << m_request.From << std::endl;
                std::cout << "To:" << m_request.To << std::endl;
                std::cout << "Verion:" << m_request.Version << std::endl;
                m_request.XmlBodyNew();
                m_socket.async_read_some(
                    boost::asio::buffer(m_request.XmlBody, m_request.BodyLen),
                    m_strand.wrap(boost::bind(&connection::handle_read, shared_from_this(),
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred)));
                break;
            case READ_BODY:
                m_reading = READ_HEADLEN;
                std::cout << "Read size=" << bytes_transferred << " Xml body: \r\n" << m_request.XmlBody << std::endl;
                
                std::cout << "Read standby read head again" << std::endl;
                m_socket.async_read_some(
                    boost::asio::buffer(&m_request.HeadLen, sizeof(m_request.HeadLen)),
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

