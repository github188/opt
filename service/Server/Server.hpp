#ifndef SERVER_HPP
#define SERVER_HPP

#include <boost/asio.hpp>
#include <string>
#include <vector>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

#include <list>

#include "Connection.hpp"
#include "RequestHandler.hpp"
#include "Client.hpp"
#include "CRedis.hpp"

namespace service {

/// The top-level class of the server.
class server : private boost::noncopyable
{
    friend class connection;
public:
    /// Construct the server.
    explicit server(const std::string& address, 
        const std::string& port, std::size_t thread_pool_size);

    boost::asio::io_service &ioservice() { return m_io_service; }

    /// Run the server's io_service loop.
    void run();

    void run(const size_t seconds);

    /// Stop the server.
    void stop();

private:

    /// Handle completion of an asynchronous accept operation.
    void handle_accept(const boost::system::error_code& e);

    /// The number of threads that will call io_service::run().
    std::size_t m_thread_pool_size;

    /// The io_service used to perform asynchronous operations.
    boost::asio::io_service m_io_service;

    /// Acceptor used to listen for incoming connections.
    boost::asio::ip::tcp::acceptor m_acceptor;

    connect_attr_list m_connection_list;

    /// The next connection to be accepted.
    connection_ptr m_new_connection;

    /// The handler for all incoming requests.
    request_handler m_request_handler;
};

} // service

#endif // SERVER_HPP

