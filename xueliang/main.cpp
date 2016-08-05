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
        std::size_t num_threads = boost::lexical_cast<std::size_t>("3");
        xl::server s("192.168.20.120", "8090", num_threads);
        // Start server.
        s.run();
    }
    catch (std::exception& e)
    {
        std::cerr << "exception: " << e.what() << "\n";
        return -1;
    }
    return 0;
}

