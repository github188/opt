
#ifndef __HTTP_REQ_COMMON_H__
#define __HTTP_REQ_COMMON_H__

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define bool_t int
#define true 1
#define false 0

#define COLOR_N         "\033[m"
#define COLOR_R         "\033[0;32;31m"
#define COLOR_G         "\033[0;32;32m"
#define COLOR_P         "\033[0;35m"
#define COLOR_W         "\033[1;37m"

#define log_err(fmt, args...) do{\
    printf(COLOR_R"[SYNC_HTTP_REQUEST]# "COLOR_N);\
    printf(fmt, ##args); \
}while(0)
    
#define log_inf(fmt, args...) do{\
    printf(COLOR_G"[SYNC_HTTP_REQUEST]# "COLOR_N);\
    printf(fmt, ##args); \
}while(0)
    

// STACK SIZE
// default 8388608(ulimit -s: 8192K); minimum 16384(16K)
#define THREAD_STACK_SIZE_K(k) ((k)*1024UL) /* valid if k >= 16 */
#define THREAD_STACK_SIZE_M(m) ((m)*1024UL*1024UL)
 
#endif
