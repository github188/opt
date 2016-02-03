#include <hmac_sha1.h>
#include <hmac_sha2.h>
#include <string.h>
#include <stdint.h>

int test_hmac_sha1()
{
    // hmac_sha1
    int i = 0;
    uint8_t in[1024] = "a=1000061&k=AKIDytaL55OwoRYDMGFzols94MDrf8URHA0N&e=1457082928&t=1454490928&r=494373279&u=3041722595&f=";    
    uint8_t out[1024] = { 0 };    
    uint8_t key[512] = "RRJoPEXyvVeZtiCwthW6N6NDq888Pk0o";    
    printf("SHA1:\r\n");
    uint32_t len = 1024;
    hmac_sha1(key, (uint32_t)strlen((char *)key), in, strlen((char *)in), out, &len);

    for (i = 0 ; i < len; i++)
        printf("%02x", out[i]);
    printf("\r\n");
    return 0;
    
}

int main() {

    uint32_t hash_len = SHA256_DIGEST_SIZE;
    uint8_t  hash[hash_len];

    const uint8_t key[]  = "Never tell";
    uint32_t       key_len = strlen((char *)key);

    const uint8_t message[]     = "I'm your little secret";
    uint32_t       message_len = strlen((char *)message);

    uint32_t i = 0;

    hmac_sha256(key, key_len, message, message_len, hash, &hash_len);
    printf("SHA256: \n");   
    for (i = 0; i < hash_len; ++i) {
        printf("%x", hash[i]);
    }
    printf("\n");
    test_hmac_sha1();
    return 0;
}
