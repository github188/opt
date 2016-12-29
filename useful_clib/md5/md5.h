#ifndef MD5_H_
#define MD5_H_

#include <stdint.h>
#include <stdlib.h>

typedef struct md5_ctx_s
{
    uint32_t state[4]; /* state (ABCD) */
    uint32_t count[2]; /* number of bits, modulo 2^64 (lsb first) */
    uint8_t buffer[64]; /* input buffer */
    uint8_t dest[16]; /* md5 dest */
    char hash[33]; /* hash string */
    char reserved[31];
} md5_ctx_t;

void md5_init(md5_ctx_t* ctx);
void md5_update(md5_ctx_t* ctx, const void* data, size_t size);
void md5_update_str(md5_ctx_t* ctx, const char* str);
//void md5_update_file(md5_ctx_t* ctx, const char* file);
uint8_t* md5_final(md5_ctx_t* ctx);
char* md5_hash(md5_ctx_t* ctx);
char* md5_hash_lower(md5_ctx_t* ctx);
char * md5_str(const char *string, char *md5_result) ;

#endif/*MD5_H_*/
