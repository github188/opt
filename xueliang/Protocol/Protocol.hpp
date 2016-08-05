#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include "tinyxml2.h"

typedef enum _XL_ServerCom
{
    /* 系统综合管理平台 */
    XL_SC_CTRL_CENTER               = 0x00000000,   
    /* 信令服务 */  
    XL_SC_MSG_SERVER                = 0x00100000,
    /* 报警服务 */
    XL_SC_ALARM_SERVER              = 0x00200000,
    /* 存储服务 */
    XL_SC_STORAGE_SERVER            = 0x00300000,
    /* 车辆管理服务 */      
    XL_SC_VEHICLE_SERVER            = 0x00400000,
    /* 门禁管理服务 */      
    XL_SC_ACCESS_CTRL_SERVER        = 0x00500000,
    /* 机顶盒客户端 */  
    XL_SC_TVBOX_CLIENT              = 0x00600000,
    /* 华迈云客户端 */          
    XL_SC_HMCLIENT                  = 0x00700000,   
    /* WEB管理平台 */
    XL_SC_WEB_UI                    = 0x00800000,   
    /* TO All所有组件关注，用于推送通知 */
    XL_SC_ALL                       = 0x0FF00000
} XL_ServerCom;

typedef enum _XL_Error
{
    /* 无错误 */
    XL_ERR_NONE                     = 0x00000000,
    /* 不识别指令 */
    XL_ERR_UNREG_CMD                = 0x00000001,
    /* 错误的参数 */
    XL_ERR_BAD_PARAM                = 0x00000002,
    /* 不识别服务组件 */
    XL_ERR_UNKOWN_SERVER            = 0x00000003,
    /* 系统错误（系统错误码）*/
    XL_ERR_SYSTEM                   = 0x00000004,
    /* 系统忙 */
    XL_ERR_SYSTEM_BUSY              = 0x00000005,
    /* 格式错误 */
    XL_ERR_FORMAT                   = 0x00000006,
    /* 未识别的错误 */
    XL_ERR_UNKOWN                   = 0x0000000F
} XL_Error;

struct XL_MsgHead
{
    XL_MsgHead()
    {
        HeadLen = sizeof(XL_MsgHead);
        Id = 0;
        BodyLen = 0;
        Session = 0;
        Error = 0;
        Version = 0;
        From = 0;
        To = 0;
    }
    char *HeadBody()
    {
        return (char *)(this) + sizeof(HeadLen);
    }
    int HeadBodyLen()
    {
        return sizeof(XL_MsgHead) - sizeof(HeadLen);
    }
    int HeadLen;        /* 本消息头总长度 */
    int Id;             /* 消息ID */
    int BodyLen;        /* 消息体长度 */
    int Session;        /* 会话ID */
    int Error;          /* 错误码 */
    int Version;        /* 协议版本 */
    int From;           /* 消息源组件ID */
    int To;             /* 消息目的组件ID */
} ; 

struct reply : public XL_MsgHead
{
    reply() { XmlBody = nullptr; }
    void XmlBodyNew()
    {
        XmlBody = new char[BodyLen];
    }
    void XmlBodyDelete()
    {
        delete [] XmlBody;
        XmlBody = nullptr;
    }
    tinyxml2::XMLDocument XmlDoc;
    char *XmlBody;
};

struct request : public XL_MsgHead
{
    request() { XmlBody = nullptr; }
    void XmlBodyNew()
    {
        XmlBody = new char[BodyLen];
    }
    void XmlBodyDelete()
    {
        delete [] XmlBody;
        XmlBody = nullptr;
    }
    tinyxml2::XMLDocument XmlDoc;
    char *XmlBody;
};

#endif /* PROTOCOL_HPP */


