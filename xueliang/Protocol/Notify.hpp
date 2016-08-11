#ifndef NOTIFY_HPP
#define NOTIFY_HPP

#include "ChannelTemplate.hpp"
#include "json.h"

class CNotify 
{
public:

    CNotify();
    ~CNotify();

    void Parse(const std::string &jsonstring);

    void Dump();

private:
    Json::Reader m_jreader;
    Json::Value m_root;
};

#endif /* NOTIFY_HPP */

