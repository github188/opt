#ifndef REQUEST_HANDLER_HPP
#define REQUEST_HANDLER_HPP

#include <string>
#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>

#include "Protocol.hpp"

namespace service {

/// The common handler for all incoming requests.
class request_handler
{
public:
    explicit request_handler();

    void handle_request(const hm_message& req, hm_message& rep);
};

} // service

#endif // REQUEST_HANDLER_HPP

