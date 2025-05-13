#pragma once

#include <stdarg.h>

// ─── Log Levels ──────────────────────────────────────────────
typedef enum {
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

// ─── Core Logging Function ───────────────────────────────────
void log_msg(LogLevel level, const char *fmt, ...);

// ─── Convenience Macros ──────────────────────────────────────
#define LOG_INFO(...)  log_msg(LOG_LEVEL_INFO,  __VA_ARGS__)
#define LOG_WARN(...)  log_msg(LOG_LEVEL_WARN,  __VA_ARGS__)
#define LOG_ERROR(...) log_msg(LOG_LEVEL_ERROR, __VA_ARGS__)
