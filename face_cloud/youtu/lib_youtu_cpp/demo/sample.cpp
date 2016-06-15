#include <sys/time.h>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <stdlib.h>
#include "include/ytopen_sdk.h"

using namespace std;
using namespace rapidjson;

void DetectFace(ytopen_sdk &sdkentry)
{
    rapidjson::Document result;
    if(0 != sdkentry.DetectFace(result, "a.jpg"))
    {
        cout << "DetectFace failed." << endl;
    }

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    result.Accept(writer);
    cout << buffer.GetString() << endl;
}

void face_detection_callback(uv_timer_t *handle )
{
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

    DetectFace(m_sdk);
}

int main(int argc,char * argv[])
{
    uv_timer_t timer;
    cout << "uv timer" << endl;
    uv_timer_init(uv_default_loop(), &timer);
    uv_timer_start(&timer, face_detection_callback, 0, 1000);

    cout << "uvloop" << endl;
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    uv_timer_stop(&timer);
    uv_close((uv_handle_t *)&timer);
    return 0;
}

