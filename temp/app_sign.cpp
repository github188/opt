#include <stdio.h>
#include <time.h>
#include "app_sign.hpp"

CSign::CSign()
{
}

CSign::~CSign()
{
}

void CSign::SetAppManInfo(string qq, string aId, string sId, string sKey)
{
    m_qq = qq;
    m_AppId = aId;
    m_SecretId = sId;
    m_SecretKey = sKey;
    printf("U:%s AID:%s SID:%s SKEY:%s\n", m_qq.c_str(), m_AppId.c_str(), m_SecretId.c_str(), m_SecretKey.c_str());
}

void CSign::SetAppManInfo(
    const char* qq, const char* aId, const char* sId, const char* sKey)
{
    m_qq.assign(qq);
    m_AppId.assign(aId);
    m_SecretId.assign(sId);
    m_SecretKey.assign(sKey);
    printf("U:%s AID:%s SID:%s SKEY:%s\n", m_qq.c_str(), m_AppId.c_str(), m_SecretId.c_str(), m_SecretKey.c_str());
}

string CSign::GetSign()
{
    string sign;
    // u=10000
    // &a=2011541224
    // &k=AKID2ZkOXFyDRHZRlbPo93SMtzVY79kpAdGP
    // &e=1432970065
    // &t=1427786065
    // &r=270494647
    // &f=
    // More infomation @ http://open.youtu.qq.com/welcome/authentication
    time_t tnow = time(0);
    char ts[64] = "";
    char ets[64] = "";
    snprintf(ts, sizeof(ts), "%ld", (long int)tnow);
    snprintf(ets, sizeof(ets), "%ld", (long int)(tnow + 3600 * 30)); /* valid in a month */
    string orignal("u=");
    orignal.append(m_qq);
    orignal.append("&a=");
    orignal.append(m_AppId);
    orignal.append("&k=");
    orignal.append(m_SecretId);
    orignal.append("&e=");
    orignal.append(ets);
    orignal.append("&t=");
    orignal.append(ts);
    orignal.append("&r=");
    orignal.append("1234567890");
    orignal.append("&f="); /* not set */

    // Sign= Base64(HMAC-SHA1(SecretKey, orignal) + original)
    return sign;
}

// TEST

int main()
{
    CSign sign;
    sign.SetAppManInfo("1162118860","2011541224", 
        "AKID2ZkOXFyDRHZRlbPo93SMtzVY79kpAdGP", 
        "ckKU7P4FwB4PBZQlnB9hfBAcaKZMeUge");
    return 0;
}
