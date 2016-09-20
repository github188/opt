#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "utf8_ucs2.h"

#define TEST_INTERFACE_PARAM(interface, param, prefix) do{\
    char *result = interface(param);\
    printf("[%-16s:%08d: %s]: %s\n", __FILE__, __LINE__, prefix, result);\
    free(result);\
} while(0)

#define TEST_INTERFACE_PARAM_LEN(interface, param, len, prefix) do{\
    char *result = interface(param, len);\
    printf("[%-16s:%08d: %s]: %s\n", __FILE__, __LINE__, prefix, result);\
    free(result);\
} while(0)

#define TEST_INTERFACE_PARAM_LEN_HEX(interface, param, len, prefix) do{\
    char *result = interface(param, len);\
    trace_inhex(prefix, __FILE__, __LINE__, result, *((int *)len));\
    free(result);\
} while(0)

int main(int argc, const char *argv[])
{
    // FEFF: Big endian
    TEST_INTERFACE_PARAM(strucs2_to_hexutf8,
        "FEFF002B0038003600310038003900380031003900390033003300300034",
        "Big endian UCS2 string to UTF8 hex buffer.              <Show in STR>");
    // FFFE: Little endian
    TEST_INTERFACE_PARAM(strucs2_to_hexutf8,
        "FFFE6100620063004B6DD58B31003300340035003600",
        "Little endian UCS2 string to UTF8 hex buffer.           <Show in STR>");

    char ucsbuf[16] = { 
        0x61, 0x00, 
        0x4E, 0x53, 
        0x3A, 0x4E, 
        0x61, 0x00, 
        0x0d, 0x00, 0x0a, 0x00, 
        0x00, 0x00 }; 
    TEST_INTERFACE_PARAM_LEN(hexucs2_to_hexutf8, ucsbuf, sizeof(ucsbuf),
        "UCS2 hex buffer to UTF8 hex.                            <Show in STR>");

    char utf8buf[16] = {
        0xE6, 0xB5, 0x8B,
        0xE8, 0xAF, 0x95,
        0xE7, 0xA8, 0x8B,
        0xE5, 0xBA, 0x8F 
    };
    int len = 0;
    
    TEST_INTERFACE_PARAM_LEN_HEX(hexutf8_to_hexucs2, utf8buf, &len,
        "UTF8 hex buffer to UCS2 hex.                            <Show in HEX>");

    TEST_INTERFACE_PARAM_LEN(hexutf8_to_strucs2, utf8buf, &len,
        "UTF8 hex buffer to UCS2 string.                         <Show in STR>");

    if(argv[1])
    {
        TEST_INTERFACE_PARAM_LEN_HEX(hexutf8_to_hexucs2, argv[1], &len,
        "UTF8 hex buffer to UCS2 hex.                            <Show in HEX>");
        TEST_INTERFACE_PARAM_LEN(hexutf8_to_strucs2, argv[1], &len,
        "UTF8 hex buffer to UCS2 string.                         <Show in STR>");
    }

    return 0;
}
