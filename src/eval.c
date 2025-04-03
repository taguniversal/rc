#include "sqlite3.h"
#include <stddef.h> 
#include <string.h>
#include <time.h>
#include <stdio.h>     // fprintf, stderr
#include <stdlib.h>    // general utilities (optional but common)
#include <time.h>      // time_t, localtime, strftime
#include <unistd.h>    // isatty, fileno
#include "rdf.h"
#include "log.h"



void eval_expression_literal_only(sqlite3* db, const char* expr_id) {
    LOG_INFO("🔍 Literal eval for: %s\n", expr_id);

    // Step 1: Get place_of_resolution from expr_id
    const char* por = lookup_object(db, expr_id, "inv:place_of_resolution");
    if (!por) {
        LOG_WARN("No PlaceOfResolution found.\n");
        return;
    }

    // Step 2: Get first ExpressionFragment
    const char* frag = lookup_object(db, por, "inv:hasExpressionFragment");
    if (!frag) {
        LOG_WARN("No ExpressionFragment found.\n");
        return;
    }

    // Step 3: Get targetPlace from fragment
    const char* destination_place = lookup_object(db, frag, "inv:DestinationPlace");
    if (!destination_place) {
        LOG_WARN("No DestinationPlace in fragment.\n");
        return;
    }

    // Step 4: Look for literal content in that DestinationPlace
    const char* content = lookup_value(db, destination_place, "inv:content");
    if (!content) {
        LOG_WARN("No content found at Destination Place: %s\n", destination_place);
        return;
    }

    // Just use the raw content directly (no angle bracket parsing)
    LOG_INFO("✅ Direct value used: %s\n", content);
    insert_triple(db, "XXX", destination_place, "inv:hasValue", content);
}


void eval(sqlite3* db) {
    LOG_INFO("⚙️  Running eval() pass\n");

    sqlite3_stmt* stmt;
    const char* sql = "SELECT subject FROM triples WHERE predicate = 'rdf:type' AND object = 'inv:Expression'";
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
