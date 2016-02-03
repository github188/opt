#include <sys/time.h>
#include <fstream>
#include <iostream>
#include "../ytopen_sdk.h"

using namespace std;
using namespace rapidjson;

int main(int argc,char * argv[])
{
    std::string encode("test");
    encode = b64_encode(encode);
    std::string decode = b64_decode(encode);
    
    //app sign params.
    ytopen_sdk::AppSign m_app_sign = 
    {
        1000061,
        "AKIDytaL55OwoRYDMGFzols94MDrf8URHA0N",
        "RRJoPEXyvVeZtiCwthW6N6NDq888Pk0o",
        "3041722595"
    };

    ytopen_sdk m_sdk;
    m_sdk.Init(m_app_sign);

    rapidjson::Document result;
    if(0 != m_sdk.DetectFace(result, "a.jpg"))
    {
        cout << "DetectFace failed." << endl;
    }

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    result.Accept(writer);
    cout << buffer.GetString() << endl;

    
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    return 0;
}
