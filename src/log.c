#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include "log.h"

#define COLOR_RESET   "\x1b[0m"
#define COLOR_INFO    "\x1b[36m"
#define COLOR_WARN    "\x1b[33m"
#define COLOR_ERROR   "\x1b[31m"

static const char *level_str[] = {"INFO", "WARN", "ERROR"};
static const char *level_color[] = {COLOR_INFO, COLOR_WARN, COLOR_ERROR};

void log_msg(LogLevel level, const char *fmt, ...)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    va_list args;
    va_start(args, fmt);

    if (isatty(fileno(stderr)))
    {
        fprintf(stderr, "%s[%s] [%s] ", level_color[level], time_buf, level_str[level]);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "%s\n", COLOR_RESET);
    }
    else
    {
        fprintf(stderr, "[%s] [%s] ", time_buf, level_str[level]);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
    }

    va_end(args);
}
