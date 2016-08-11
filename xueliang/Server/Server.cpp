
#include <boost/asio.hpp>


#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <vector>

#include "Server.hpp"

namespace xl {

server::server(const std::string& address, 
    const std::string& port, 
    std::size_t thread_pool_size): 
    
    // member initialization
    m_thread_pool_size(thread_pool_size),
    m_acceptor(m_io_service),
    m_new_connection(new connection(m_io_service, m_request_handler)),
    m_request_handler(), m_redis(nullptr) 
{
    // Open the acceptor with the option to reuse the address (i.e. SO_REUSEADDR).
    boost::asio::ip::tcp::resolver resolver(m_io_service);
    boost::asio::ip::tcp::resolver::query query(address, port);
    boost::asio::ip::tcp::endpoint endpoint = *resolver.resolve(query);
    m_acceptor.open(endpoint.protocol());
    m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    m_acceptor.bind(endpoint);
    m_acceptor.listen();
    m_acceptor.async_accept(m_new_connection->socket(),
        boost::bind(&server::handle_accept, this,
        boost::asio::placeholders::error));
}

void server::run()
{
    // Create a pool of threads to run all of the io_services.
    std::vector<boost::shared_ptr<boost::thread> > threads;
    for (std::size_t i = 0; i < m_thread_pool_size; ++i)
    {
        boost::shared_ptr<boost::thread> thread(new boost::thread(
        boost::bind(&boost::asio::io_service::run, &m_io_service)));
        threads.push_back(thread);
    }

    // Wait for all threads in the pool to exit.
    for (std::size_t i = 0; i < threads.size(); ++i)
        threads[i]->join();
}

void server::run(const size_t seconds)
{
    // Create a pool of threads to run all of the io_services.
    std::vector<boost::shared_ptr<boost::thread> > threads;
    for (std::size_t i = 0; i < m_thread_pool_size; ++i)
    {
        boost::shared_ptr<boost::thread> thread(new boost::thread(
        boost::bind(&boost::asio::io_service::run, &m_io_service)));
        threads.push_back(thread);
    }

    boost::asio::deadline_timer timer(m_io_service);
    timer.expires_from_now(boost::posix_time::seconds(seconds));
    timer.async_wait(boost::bind(&server::stop, this));

    // Wait for all threads in the pool to exit.
    for (std::size_t i = 0; i < threads.size(); ++i)
        threads[i]->join();
}


void server::redis_push(CRedis *redis)
{
    m_redis = redis;
}

void server::stop()
{
    std::cout << "disconnect redis" << std::endl;
    m_redis->disconnect();
    std::cout << "stop ioservice" << std::endl;
    m_io_service.stop();
}

void server::handle_accept(const boost::system::error_code& e)
{
    if (!e)
    {
        m_new_connection->start();
        m_new_connection.reset(new connection(m_io_service, m_request_handler));
        m_acceptor.async_accept(m_new_connection->socket(),
            boost::bind(&server::handle_accept, this,
            boost::asio::placeholders::error));
    }
}

} // xl

