
#include <time.h>
#include <unistd.h>
#include <stdio.h>

#define COLOR_RESET   "\x1b[0m"
#define COLOR_INFO    "\x1b[36m"
#define COLOR_WARN    "\x1b[33m"
#define COLOR_ERROR   "\x1b[31m"

#define LOG(level, color, fmt, ...) do { \
    time_t now = time(NULL); \
    struct tm *tm_info = localtime(&now); \
    char time_buf[20]; \
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info); \
    if (isatty(fileno(stderr))) { \
        fprintf(stderr, "%s[%s] [%s] " fmt "%s\n", color, time_buf, level, ##__VA_ARGS__, COLOR_RESET); \
    } else { \
        fprintf(stderr, "[%s] [%s] " fmt "\n", time_buf, level, ##__VA_ARGS__); \
    } \
} while (0)

#define LOG_INFO(fmt, ...)  LOG("INFO",  COLOR_INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  LOG("WARN",  COLOR_WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG("ERROR", COLOR_ERROR, fmt, ##__VA_ARGS__)


#define PSI_BLOCK_LEN 39
#define OSC_PORT_XMIT 4242
#define OSC_PORT_RECV 4243
#define BUFFER_SIZE 1024
#define IPV6_ADDR "fc00::1"
