#include <iostream>
#include "Notify.hpp"
CNotify::CNotify()
{
}

CNotify::~CNotify()
{
}

void CNotify::Parse(const std::string &jsonstring)
{
    m_jreader.parse(jsonstring, m_root);
}

void CNotify::Dump()
{
    Json::Value::Members member(m_root.getMemberNames());
    Json::Value memberVal;
    for(Json::Value::Members::iterator it = member.begin(); it != member.end(); it ++ )
    {
        memberVal = m_root[*it];
        std::cout << memberVal.toStyledString() << std::endl;
    }
}

