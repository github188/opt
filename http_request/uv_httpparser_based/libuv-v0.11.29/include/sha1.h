/*
 * sha1.h
 *
 *  Created on: 2014-12-27
 *      Author: wang.guilin
 */

#ifndef SHA1_H_
#define SHA1_H_

#include <string.h>
#include <stdlib.h>
#include <stdint.h>  /* for u_int*_t */
#include <endian.h>

/*
 SHA-1 in C
 By Steve Reid <steve@edmweb.com>
 100% Public Domain
 */
typedef struct
{
    uint32_t state[5];
    uint32_t count[2];
    unsigned char buffer[64];
} SHA1_CTX;

void SHA1Init(SHA1_CTX* context);
void SHA1Update(SHA1_CTX* context, const unsigned char* data, uint32_t len);
void SHA1Final(unsigned char digest[20], SHA1_CTX* context);
void SHA1Hash(const void* input, const uint32_t size1, void* output, const uint32_t size2);



#endif /* SHA1_H_ */
