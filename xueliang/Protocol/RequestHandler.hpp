#ifndef REQUEST_HANDLER_HPP
#define REQUEST_HANDLER_HPP

#include <string>
#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>

#include "Protocol.hpp"

namespace xl {

/// The common handler for all incoming requests.
class request_handler
{
public:
    explicit request_handler();

    void handle_request(const request& req, reply& rep);

private:
    int m_from;
    int m_to;
};

} // xl

#endif // REQUEST_HANDLER_HPP

