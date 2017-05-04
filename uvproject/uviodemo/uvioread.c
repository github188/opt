#include "uv.h"

#define PRINT(fmt...) \
    do {\
        printf("[%-8s %-18s %-4d]: \033[0;31m", "UVIO", __FUNCTION__, __LINE__);\
        printf(fmt);\
        printf("\033[0;39m");\
    }while(0)

char buffer[4096] = { 0 };
uv_fs_t read_req, open_req;
uv_buf_t bufs;
uv_timer_t timer;

void uv_fs_read_cb(uv_fs_t* req);

void uv_fs_timer_cb(uv_timer_t *handle)
{
    uv_fs_t* req = handle->data;
    uv_fs_read(req->loop, &read_req, open_req.result, &bufs, 1, req->result, uv_fs_read_cb);
}

void uv_fs_read_cb(uv_fs_t* req)
{
    uv_fs_req_cleanup(req);
    if(req->result > 0)
    {
        PRINT("Read size %d\n", req->result);
    }
#if 1 /* off */
    bufs = uv_buf_init(buffer, sizeof(buffer));
    timer.data = req;
    uv_timer_start(&timer, uv_fs_timer_cb, 1, 0);
#else
    // CPU 100%
    uv_fs_read(req->loop, &read_req, open_req.result, &bufs, 1, req->result, uv_fs_read_cb);
#endif /* 0 off */
}

void uv_fs_open_cb(uv_fs_t* req)
{
    if(req->result != -1)
    {
        bufs = uv_buf_init(buffer, sizeof(buffer));
        PRINT("Open %s ok fd = %d\n", req->path, req->result);
        uv_fs_read(req->loop, &read_req, req->result, &bufs, 1, 0, uv_fs_read_cb);
    }
    else
    {
        PRINT("Open %s failed with %s!\n", req->path, uv_err_name(errno));
    }
    uv_fs_req_cleanup(req);
}

int main(int argc, char *argv[])
{
    if(argc < 1)
    {
        PRINT("Usage: %s [filepath]\n", argv[0]);
        return -1;
    }
    
    uv_loop_t *loop = uv_default_loop();
    uv_timer_init(loop, &timer);
    
    int flags = O_CREAT | O_RDWR;
    int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
    
    PRINT("Open file %s\n", argv[1]);
    uv_fs_open(loop, &open_req, argv[1], flags, mode, uv_fs_open_cb);

    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}


