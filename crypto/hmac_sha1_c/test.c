#include "hmac_sha1.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main()
{
    int i = 0, j = 0;
    char in[1024] = "love is power";    
    char out[1024] = { 0 };    
    char key[512] = "abc";    
    
    hmac_sha1(out, key, strlen(key), in, strlen(in));
    for (i = 0 ; i < 512; i++)
        printf("%02x", out[i]);
    printf("\r\n");
    return 0;
}