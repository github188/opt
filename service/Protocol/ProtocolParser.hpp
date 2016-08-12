#ifndef PROTOCOL_PARSER_HPP
#define PROTOCOL_PARSER_HPP

#include "tinyxml2.h"

class protocol_parser
{
public:
    /// Construct ready to parse request message
    protocol_parser();
    /// Rest to initial parser state.
    reset();
    template <typename MESSAGE>
    boost::tuple<boost::tribool, MESSAGE> parse(request &req, MESSAGE &msg)
    {
        while(begin != end)
        {
            boost::tribool result = true;
        }
    }
    
};


#endif /// PROTOCOL_PARSER_HPP

