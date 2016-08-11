#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/lexical_cast.hpp>

#include "Server.hpp"

int main(int argc, char* argv[])
{
    try
    {
        // Initialise server.
        std::size_t num_threads = boost::lexical_cast<std::size_t>("4");
        xl::server s("192.168.20.120", "8090", num_threads);

        // Redis client
        CRedis redis(s.ioservice());
        // Redis server not protected
        redis.connect("192.168.20.120", 6379, "admin");
        //redis.connect();
        s.redis_push(&redis);

        // Test base client
        //Client c(s.ioservice());
        //c.connect("192.168.20.120", 80);

        // Start server.
        s.run(3);
    }
    catch (std::exception& e)
    {
        std::cerr << "exception: " << e.what() << "\n";
        return -1;
    }
    return 0;
}

