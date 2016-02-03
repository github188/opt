#ifndef __APP_SIGN__
#define __APP_SIGN__

#include <iostream>
#include <string>

using namespace std;

typedef std::string string;

class CSign
{
public:
    CSign();
    ~CSign();
    void SetAppManInfo(string qq, string aId, string sId, string sKey);
    void SetAppManInfo(const char* qq, const char* aId, const char* sId, const char* sKey);
    string GetSign();

private:
    string m_qq; /* Devloper qq number */
    string m_AppId;
    string m_SecretId;
    string m_SecretKey;
    string m_Sign;
};


#endif /* __APP_SIGN__ */

