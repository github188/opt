#ifndef CHANNEL_TEMPLATE_HPP
#define CHANNEL_TEMPLATE_HPP

#include <vector>
#include <boost/noncopyable.hpp>

struct CRegistChannels : public boost::noncopyable
{
    CRegistChannels()
    {
        channels.push_back("HM.CONFIG");
        channels.push_back("HM.ALARM");
        channels.push_back("HM.STATE");
        channels.push_back("HM.LOG");

        // Self-control
        // stop
        channels.push_back("CTRL");
        channels.push_back("TEST");
    };
    ~CRegistChannels()
    {
        channels.clear();
    };

    std::vector<std::string> channels;
};

#endif /* CHANNEL_TEMPLATE_HPP */

