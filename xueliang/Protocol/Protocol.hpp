#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include "tinyxml2.h"

typedef enum _XL_ServerCom
{
    /* ϵͳ�ۺϹ���ƽ̨ */
    XL_SC_CTRL_CENTER               = 0x00000000,   
    /* ������� */  
    XL_SC_MSG_SERVER                = 0x00100000,
    /* �������� */
    XL_SC_ALARM_SERVER              = 0x00200000,
    /* �洢���� */
    XL_SC_STORAGE_SERVER            = 0x00300000,
    /* ����������� */      
    XL_SC_VEHICLE_SERVER            = 0x00400000,
    /* �Ž�������� */      
    XL_SC_ACCESS_CTRL_SERVER        = 0x00500000,
    /* �����пͻ��� */  
    XL_SC_TVBOX_CLIENT              = 0x00600000,
    /* �����ƿͻ��� */          
    XL_SC_HMCLIENT                  = 0x00700000,   
    /* WEB����ƽ̨ */
    XL_SC_WEB_UI                    = 0x00800000,   
    /* TO All���������ע����������֪ͨ */
    XL_SC_ALL                       = 0x0FF00000
} XL_ServerCom;

typedef enum _XL_Error
{
    /* �޴��� */
    XL_ERR_NONE                     = 0x00000000,
    /* ��ʶ��ָ�� */
    XL_ERR_UNREG_CMD                = 0x00000001,
    /* ����Ĳ��� */
    XL_ERR_BAD_PARAM                = 0x00000002,
    /* ��ʶ�������� */
    XL_ERR_UNKOWN_SERVER            = 0x00000003,
    /* ϵͳ����ϵͳ�����룩*/
    XL_ERR_SYSTEM                   = 0x00000004,
    /* ϵͳæ */
    XL_ERR_SYSTEM_BUSY              = 0x00000005,
    /* ��ʽ���� */
    XL_ERR_FORMAT                   = 0x00000006,
    /* δʶ��Ĵ��� */
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
    int HeadLen;        /* ����Ϣͷ�ܳ��� */
    int Id;             /* ��ϢID */
    int BodyLen;        /* ��Ϣ�峤�� */
    int Session;        /* �ỰID */
    int Error;          /* ������ */
    int Version;        /* Э��汾 */
    int From;           /* ��ϢԴ���ID */
    int To;             /* ��ϢĿ�����ID */
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


