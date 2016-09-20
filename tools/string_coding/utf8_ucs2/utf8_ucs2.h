#ifndef UTF8_TO_UCS2_H
#define UTF8_TO_UCS2_H

void trace_inhex(char *brief, char *file, int line, const char *str, int len);
char *strucs2_to_hexutf8(const char *ucs2str);
char *hexucs2_to_hexutf8(const char *ucs2hex, int len);
char *hexutf8_to_hexucs2(const char *utf8hex, int *ucs2len);
char *hexutf8_to_strucs2(const char *utf8hex, int *ucs2len);

#endif 
