#include <string.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "utf8_ucs2.h"

/**
 *---------------------------------------------------
 * UCS2  : UTF8
 *  u16    1 Bytes 0xxxxxxx 
 *  u16    2 Bytes 110xxxxx 10xxxxxx 
 *  u16    3 Bytes 1110xxxx 10xxxxxx 10xxxxxx 
 *---------------------------------------------------
 */
typedef unsigned short ucs2; /* Unicode2 16bits. */
typedef unsigned char utf8;  /* UTF-8 8bits. */
#define UCS2_BIG_ENDIAN 1
#define UCS2_LIT_ENDIAN 2

/**
 *
 * @Description:
 *      
 *   Convert UTF8-coded characters into Unicode2-coded. 
 *
 * @Params
 *      <dst> : A pointer to indicate the memmory address which was used to store 
 *   Unicode2-coded characters. This functions never check args(dst, src), 
 *   after Convertion dst ends with zero.
 *      <src> : A pointer to indicate the memmory address which will be converted.
 *   Before invoke this functions, you should assure that two pointer is valid
 *   and you have adequte memmory ,that is, the length of dst no less than src.
 *      <ucs2str> : A pointer to indicate the memmory address which will be 
 *   converted hex value string. memmory size is 2 * the length of(dst).
 *
 * @Return value:
 *   It Always should be dst.
 *
 *   see also ucs2utf.
 */
static ucs2 *utf2ucs (ucs2 *dst, utf8 *src, char *ucs2str, int *ucs2len)
{
    unsigned int i = 0;
    unsigned int j = 0;
    unsigned int next = 0;

    while (src[i] != '\0')
    {
        ucs2 temp = 0;
        if ((src[i] & 0xF0) == 0xE0 && 
            (src[i+1] & 0xC0) == 0x80 && (src[i+2] & 0xC0) == 0x80)
        {
            next = 3;
            temp |= ((src[i] & 0xF) << 12);
            temp |= ((src[i+1] & 0x3F) << 6);
            temp |= ((src[i+2] & 0x3F) << 0);
        }
        else if((src[i] & 0xE0) == 0xC0 && (src[i+1] & 0xC0) == 0x80)
        {
            next = 2;

            temp |= (src[i] & 0x1F) << 6;
            temp |= (src[i+1] & 0x3F) << 0;

        }
        else
        {
            next = 1;
            temp = src[i];
        }
        if(ucs2str)
        {
            sprintf(ucs2str+j*4, "%04X", temp);
        }
        dst[j++] = temp;
        i += next;
    }
    dst[j] = 0;
    *ucs2len = j*2;
    return dst;
};


/**
 *    
 * Convert Unicode2-coded characters into UTF8-coded. 
 *
 * see also utf2ucs. 
 *
 * Note that utf-8 string's length can be get from <strlen>
 *
 */
static utf8 *ucs2utf (utf8 *dst, ucs2 *src)
{
    unsigned int i = 0;
    unsigned int j = 0;
    unsigned int next = 0;

    while (src[i] != 0)
    {
        if (src[i] < 0x80)
        { 
            next = 1;
            dst[j] = 0;
            dst[j] = src[i];
        }
        else if(src[i] < 0x800)
        {
            next = 2;
            dst[j] = 0;
            dst[j+1] = 0;
            dst[j+1] = (utf8)((src[i] & 0x3F) | 0x80);
            dst[j] = (utf8)(((src[i] & 0x3F) & 0x1F) | 0xC0);
        }
        else
        {
            next = 3;
            dst[j] = 0;
            dst[j+1] = 0;
            dst[j+2] = 0;
            dst[j] |= ((((utf8)(src[i] >> 12)) & 0xF) | 0xE0);
            dst[j+1] |= (((utf8)(src[i] >> 6) & 0x3F) | 0x80);
            dst[j+2] |= (((utf8)(src[i] >> 0) & 0x3F) | 0x80);
        }
        j += next;
        i++;
    }
    dst[j] = 0;
    return dst;
}

// Endian swap
static char * ucs2_swap_to_hex(const char *ucs2str, char *ucs2hexbuf)
{
    if(!ucs2str || !ucs2hexbuf)
        return NULL;
    int swap = 0;
    union {
        char low;
        short bytes;
    } endian;

    endian.bytes = 0xFFFE;
    const char *p = NULL;

    int sys_endian = ((0xFF&endian.low) == 0xFE) ? UCS2_LIT_ENDIAN : UCS2_BIG_ENDIAN;
    
    if(!strncmp(ucs2str, "FFFE", 4)) /* Code little endian */
    {
        p = ucs2str + 4;
        if(sys_endian == UCS2_BIG_ENDIAN)
            swap = 1;
    }
    else if(!strncmp(ucs2str, "FEFF", 4)) /* Code big endian */
    {
        p = ucs2str + 4;
        if(sys_endian == UCS2_LIT_ENDIAN)
            swap = 1;
    }

    int i = 0;
    for( i = 0; i < strlen(p)/2; i++ )
    {
        unsigned int bit2 = 0;
        sscanf(p+i*2, "%02X", &bit2);
        ucs2hexbuf[i] = (char)bit2;
        if( i >= 1 && i%2 != 0 && swap)
        {
            char temp = ucs2hexbuf[i-1];
            ucs2hexbuf[i-1] = ucs2hexbuf[i];
            ucs2hexbuf[i] = temp;
        }
    }
    return ucs2hexbuf;
}

/**
 *
 * Interfaces
 *
 * Warning: all retrun strings is malloced or calloced, need be free after used.
 *
 */

/**
 * @Description
 *
 *   Show hex value of param <str> by byte.
 *
 * @Params
 *
 *   <brief>: Brief prefix
 *   <str>  : String to be show
 *   <len>  : Buffer len of <str>
 */
void trace_inhex(char *brief, char *file, int line, const char *str, int len)
{
    int i;
    if(!str)
        return;
    printf("[%-16s:%08d: %s]: ", file, line, brief);
    for(i = 0; i < len; i++)
    {
        printf("%02X", str[i] & 0xff);
    }
    printf("\r\n");
}

/**
 * @Description
 *
 *   Convert Unicode-2 decoded string to utf8-encoded hex buffer.
 *
 * @Params
 *
 *   <ucs2str>: Unicode-2 encoded string
 *
 * @Return value:
 *
 *   UTF-8 encoded buffer pointer which need free after real use.   
 */
char *strucs2_to_hexutf8(const char *ucs2str)
{
    if(!ucs2str)
        return NULL;

    char *utf8_buf = calloc(1, strlen(ucs2str) * 4); /* maxmum */
    
    char *ucs2hexbuf = calloc(1, strlen(ucs2str));

    ucs2_swap_to_hex(ucs2str, ucs2hexbuf);

    ucs2utf((utf8 *)utf8_buf, (ucs2 *)ucs2hexbuf);

    free(ucs2hexbuf);
    
    return utf8_buf;
}

/**
 * @Description
 *
 *   Convert Unicode-2 decoded buffer to utf8-encoded hex buffer.
 *
 * @Params
 *
 *   <ucs2str>: Unicode-2 encoded string
 *
 * @Return value:
 *
 *   UTF-8 encoded buffer pointer which need free after real use.   
 */
char *hexucs2_to_hexutf8(const char *ucs2hex, int len)
{
    if(!ucs2hex)
        return NULL;
    ucs2 * ucs2buf = (ucs2 *)ucs2hex;

    utf8 * utf8buf = calloc(1, len * 4);

    return (char *)ucs2utf(utf8buf, ucs2buf);
}


char *hexutf8_to_hexucs2(const char *utf8hex, int *ucs2len)
{
    if(!utf8hex || !ucs2len)
        return NULL;

    utf8 *utf8buf = (utf8 *)utf8hex;
    ucs2 *ucs2buf = calloc(1, strlen(utf8hex));
    
    return (char *)utf2ucs(ucs2buf, utf8buf, NULL, ucs2len);
}

char *hexutf8_to_strucs2(const char *utf8hex, int *ucs2len)
{
    if(!utf8hex || !ucs2len)
        return NULL;

    utf8 *utf8buf = (utf8 *)utf8hex;
    ucs2 *ucs2buf = calloc(1, strlen(utf8hex));
    char *ucs2str = calloc(1, strlen(utf8hex)*2 + 1);

    utf2ucs(ucs2buf, utf8buf, ucs2str, ucs2len);

    free(ucs2buf);
    
    return ucs2str;
}


