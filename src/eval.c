#include "sqlite3.h"
#include <stddef.h> 
#include <string.h>
#include <time.h>
#include <stdio.h>     // fprintf, stderr
#include <stdlib.h>    // general utilities (optional but common)
#include <time.h>      // time_t, localtime, strftime
#include <unistd.h>    // isatty, fileno
#include "rdf.h"

#define COLOR_RESET   "\x1b[0m"
#define COLOR_INFO    "\x1b[36m"  // Cyan
#define COLOR_WARN    "\x1b[33m"  // Yellow
#define COLOR_ERROR   "\x1b[31m"  // Red


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



void eval_expression_literal_only(sqlite3* db, const char* expr_id) {
    LOG_INFO("🔍 Literal eval for: %s\n", expr_id);

    // Step 1: Get place_of_resolution from expr_id
    const char* por = lookup_object(db, expr_id, "ex:place_of_resolution");
    if (!por) {
        LOG_WARN("No PlaceOfResolution found.\n");
        return;
    }

    // Step 2: Get first ExpressionFragment
    const char* frag = lookup_object(db, por, "ex:hasExpressionFragment");
    if (!frag) {
        LOG_WARN("No ExpressionFragment found.\n");
        return;
    }

    // Step 3: Get targetPlace from fragment
    const char* target = lookup_object(db, frag, "ex:targetPlace");
    if (!target) {
        LOG_WARN("No targetPlace in fragment.\n");
        return;
    }

    // Step 4: Look for literal content in that DestinationPlace
    const char* content = lookup_value(db, target, "ex:content");
    if (!content) {
        LOG_WARN("No content found at targetPlace: %s\n", target);
        return;
    }

    // Expect content like "OUT<XYZ>"
    const char* start = strchr(content, '<');
    const char* end   = strchr(content, '>');
    if (start && end && end > start) {
        char value[64] = {0};
        strncpy(value, start + 1, end - start - 1);
        value[end - start - 1] = '\0';

        LOG_INFO("✅ Extracted value: %s\n", value);
        insert_triple(db, "XXX", target, "ex:hasValue", value);
    } else {
        LOG_WARN("Malformed content format: %s\n", content);
    }
}

void eval(sqlite3* db) {
    LOG_INFO("⚙️  Running eval() pass\n");

    sqlite3_stmt* stmt;
    const char* sql = "SELECT subject FROM triples WHERE predicate = 'rdf:type' AND object = 'ex:Expression'";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare eval query.");
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* expr_id = (const char*)sqlite3_column_text(stmt, 0);
        LOG_INFO("📘 Evaluating expression: %s\n", expr_id);

        eval_expression_literal_only(db, expr_id);
    }

    sqlite3_finalize(stmt);
}
