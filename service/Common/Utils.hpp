#ifndef UTILS_HPP
#define UTILS_HPP

#define C_N    "\033[m"
#define C_R    "\033[0;32;31m"
#define C_G    "\033[0;32;32m"
#define C_P    "\033[0;35m"
#define C_W    "\033[1;37m"

#include <cstring>

#ifndef WIN32
#define __BN__(a) (strrchr(a, '/') ? strrchr(a, '/') + 1 : a)
#define INFO(fmt, ...) \
    std::fprintf(stderr, C_W "[INFO] %s line: %u" C_N ": " fmt, __BN__(__FILE__), __LINE__, ##__VA_ARGS__)
#define IMPT(fmt, ...) \
    std::fprintf(stderr, C_G "[IMPT] %s line: %u" C_N ": " fmt, __BN__(__FILE__), __LINE__, ##__VA_ARGS__)
#define WARN(fmt, ...) \
    std::fprintf(stderr, C_P "[WARN] %s line: %u" C_N ": " fmt, __BN__(__FILE__), __LINE__, ##__VA_ARGS__)
#define EROR(fmt, ...) \
    std::fprintf(stderr, C_R "[EROR] %s line: %u" C_N ": " fmt, __BN__(__FILE__), __LINE__, ##__VA_ARGS__)
#else
#define INFO(fmt, ...)
#define IMPT(fmt, ...)
#define WARN(fmt, ...)
#define EROR(fmt, ...)
#endif
        
#endif /* UTILS_HPP */

