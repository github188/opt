#include <uv.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// 雪亮测试消息
typedef struct _XL_MsgHead
{
    int HeadLen;        /* 本消息头总长度 */
    int Id;             /* 消息ID */
    int BodyLen;        /* 消息体长度 */
    int Session;        /* 会话ID */
    int Error;          /* 错误码 */
    int Version;        /* 协议版本 */
    int From;           /* 消息源组件ID */
    int To;             /* 消息目的组件ID */
} XL_MsgHead; 

uv_stream_t *ghandle = NULL;

void on_written(uv_write_t *req, int status)
{
    printf("Write end\n");
    
    uv_write_t wr_req;
    uv_buf_t buf[2];
    const char *XmlMessage = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n\
        <Message>\r\n\
          <IP>\"192.168.20.121\"</IP>\r\n\
          <TimeStamp>\"1234567890\"</TimeStamp>\r\n\
        </Message>\r\n";
    XL_MsgHead xlhead;
    xlhead.HeadLen = sizeof(XL_MsgHead);
    xlhead.Id = 0x000008001;
    xlhead.Session = 0;
    xlhead.From = 1;
    xlhead.To = 2;
    xlhead.Version = 1;
    xlhead.Error = 0;
    xlhead.BodyLen = strlen(XmlMessage);
    
    buf[0].base = (char *)&xlhead;
    buf[0].len = xlhead.HeadLen;
    buf[1].base = (char *)XmlMessage;
    buf[1].len = xlhead.BodyLen;
    printf("Send xml size %d\n", xlhead.BodyLen);
    uv_write(&wr_req, ghandle, buf, 2, on_written);
}

void onclose(uv_handle_t *handle)
{
    if(handle)
        free(handle);
}

void on_connect(uv_connect_t* req, int status)
{
    if (status < 0) {
        fprintf(stderr, "connect failed error %s\n", uv_err_name(status));
        free(req);
        return;
    }
    uv_write_t wr_req;
    uv_buf_t buf[2];
    const char *XmlMessage = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n\
        <Message>\r\n\
          <IP>\"192.168.20.121\"</IP>\r\n\
          <TimeStamp>\"1234567890\"</TimeStamp>\r\n\
        </Message>\r\n";
    XL_MsgHead xlhead;
    xlhead.HeadLen = sizeof(XL_MsgHead);
    xlhead.Id = 0x000008001;
    xlhead.Session = 0;
    xlhead.From = 1;
    xlhead.To = 2;
    xlhead.Version = 1;
    xlhead.Error = 0;
    xlhead.BodyLen = strlen(XmlMessage);
    
    buf[0].base = (char *)&xlhead;
    buf[0].len = xlhead.HeadLen;
    buf[1].base = (char *)XmlMessage;
    buf[1].len = xlhead.BodyLen;
    printf("Send xml size %d\n", xlhead.BodyLen);
    uv_write(&wr_req, (uv_stream_t*)req->handle, buf, 2, on_written);
    if(ghandle == NULL)
        ghandle = (uv_stream_t*)req->handle;
}

int main()
{
    uv_loop_t loop;
    uv_loop_init(&loop);
    uv_tcp_t *socket = (uv_tcp_t *)malloc(sizeof(uv_tcp_t));
    uv_tcp_init(&loop, socket);
    
    uv_connect_t *connect = (uv_connect_t *)malloc(sizeof(uv_connect_t));
    struct sockaddr_in dst;
    //uv_ip4_addr("192.168.20.142", 58000, &dst);
    uv_ip4_addr("192.168.20.120", 8090, &dst);
    uv_tcp_connect(connect, socket, (const sockaddr*)&dst, on_connect);
    
    uv_run(&loop, UV_RUN_DEFAULT);
    
    uv_stop(&loop);
    uv_loop_close(&loop);
    free(socket);
    free(connect);
    
    return 0;
}

