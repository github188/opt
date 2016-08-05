#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <set>

// uvpp
#include "loop.hpp"
#include "timer.hpp"
#include "async.hpp"
#include "work.hpp"
#include "tcp.hpp"
#include "idle.hpp"
#include "resolver.hpp"

#include <memory>
#include <limits>

void myerror(uvpp::error e)
{
    if (e)
        std::cout << "UVPP Error: " << e.str() << std::endl;
}

int main()
{
    uvpp::loop loop;
    uvpp::Resolver resolver(loop);
    resolver.resolve("localhost", [](const uvpp::error& error, bool ip4, const std::string& addr)
    {
        if (error)
        {
            std::cout << error.str() << std::endl;
            return;
        }
        std::cout << (ip4 ? "IP4" : "IP6") << ": " << addr << std::endl;
    });
    
    
#if 1  
   uvpp::Tcp tcp(loop);
   if (!tcp.connect("127.0.0.1", 80, [](auto e){
       
       std::cout << "connected: " << e.str() << std::endl;
   }))
       std::cout << "error connect\n";
#endif

   std::thread t([&loop]()
   {
      std::cout << "Thread started: "<< std::this_thread::get_id() << std::endl;
      try
      {
        if (!loop.run())
        {
            std::cout << "error run\n";
        }
        std::cout << "Quit from event loop" << std::endl;
      }
      catch(...)
      {
          std::cout << "exception\n";
      }
   });

   t.join();
     
   return 0;
}
