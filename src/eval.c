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



void eval_resolution_table(sqlite3* db, const char* block, const char* expr_id) {
    LOG_INFO("🔍 Resolution table eval for: %s at block %s\n", expr_id, block);

    const char* por = lookup_object(db, expr_id, "inv:place_of_resolution");
    if (!por) return;

    // Step 1: Get ExpressionFragment
    const char* frag = lookup_object(db, por, "inv:hasExpressionFragment");
    if (!frag) return;

    // Step 2: Get destination of this fragment
    // Get the ConditionalInvocation
    const char* cond_inv = lookup_object(db, frag, "inv:hasConditionalInvocation");
    if (!cond_inv) {
        LOG_WARN("❌ No ConditionalInvocation found for resolution.\n");
        return;
    }

    // Use first hasDestinationNames as the target output
    const char* destination_place = lookup_object(db, cond_inv, "inv:hasDestinationNames");
    if (!destination_place) {
        LOG_WARN("❌ No output destination found in ConditionalInvocation.\n");
        return;
    }

    // Step 3: Get pattern inputs (assume A, B for now)
    const char* val_A = lookup_value_by_name(db, expr_id, "A");
    if (!val_A) {
        LOG_WARN("No value for A found\n");
        return;
    }
    const char* val_B = lookup_value_by_name(db, expr_id, "B");
    if (!val_B) {
        LOG_WARN("No value for B found\n");
        return;
    }

    if (!val_A || !val_B) return;

    // Step 4: Combine into key like "10"
    char key[3] = { val_A[0], val_B[0], '\0' };

    // Step 5: Look up resolution value
    LOG_INFO("🧪 Lookup resolution for key %s in expr %s\n", key, expr_id);

    const char* result = lookup_resolution(db, expr_id, key);
    if (!result) {
        LOG_WARN("❌ Resolution table missing entry for key %s\n", key);
        return;
    }

    LOG_INFO("📥 Inserting result into: %s hasValue %s\n", destination_place, result);

    insert_triple(db, block, destination_place, "inv:hasValue", result);

    const char* check = lookup_value(db, destination_place, "inv:hasValue");
    LOG_INFO("🔎 Confirmed in DB: %s hasValue %s\n", destination_place, check ? check : "(null)");
}

void eval_expression_literal_only(sqlite3* db, const char* block, const char* expr_id) {
    LOG_INFO("🔍 Literal eval for: %s\n", expr_id);

    const char* por = lookup_object(db, expr_id, "inv:place_of_resolution");
    if (!por) {
        LOG_WARN("No PlaceOfResolution found.\n");
        return;
    }

    const char* frag = lookup_object(db, por, "inv:hasExpressionFragment");
    if (!frag) {
        LOG_WARN("No ExpressionFragment found.\n");
        return;
    }

    // Step 3: Get ConditionalInvocation
    const char* cond_inv = lookup_object(db, frag, "inv:hasConditionalInvocation");
    if (!cond_inv) {
        LOG_WARN("No ConditionalInvocation found.\n");
        return;
    }

    // Step 4: Get the output DestinationPlace (assume first for now)
    const char* destination_place = lookup_object(db, cond_inv, "inv:hasDestinationNames");
    if (!destination_place) {
          LOG_WARN("No DestinationPlace in ConditionalInvocation.\n");
          return;
    }

    const char* content = lookup_value(db, destination_place, "inv:content");

    if (content) {
        LOG_INFO("✅ Direct value used: %s\n", content);
        insert_triple(db, block, destination_place, "inv:hasValue", content);
    } else {
        LOG_INFO("🧠 No literal value — trying resolution table...\n");
        eval_resolution_table(db, block, expr_id);
    }
}


int cycle(sqlite3* db, const char* block, const char* expr_id) {
    LOG_INFO("🔁 Cycle pass for expression: %s\n", expr_id);
    int side_effect = 0;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT object FROM triples WHERE psi = ? AND subject = ? AND predicate = 'inv:destination_list'";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare destination_list query.");
        return 0;
    }

    sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, expr_id, -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* dest_id = (const char*)sqlite3_column_text(stmt, 0);
        const char* dest_name = lookup_value(db, dest_id, "inv:name");

        if (!dest_name) {
            LOG_WARN("⚠️ DestinationPlace %s has no name\n", dest_id);
            continue;
        }

        sqlite3_stmt* src_stmt;
        const char* src_sql = "SELECT subject FROM triples WHERE psi = ? AND predicate = 'inv:name' AND object = ?";
        if (sqlite3_prepare_v2(db, src_sql, -1, &src_stmt, NULL) != SQLITE_OK) {
            LOG_ERROR("Failed to prepare source match query.");
            continue;
        }

        sqlite3_bind_text(src_stmt, 1, block, -1, SQLITE_STATIC);
        sqlite3_bind_text(src_stmt, 2, dest_name, -1, SQLITE_STATIC);

        if (sqlite3_step(src_stmt) == SQLITE_ROW) {
            const char* source_id = (const char*)sqlite3_column_text(src_stmt, 0);
            const char* content = lookup_value(db, source_id, "inv:hasContent");

            if (content) {
                LOG_INFO("📤 %s → %s : %s\n", source_id, dest_id, content);
                insert_triple(db, block, dest_id, "inv:hasContent", content);
                side_effect = 1;
            } else {
                LOG_INFO("🔁 Clearing %s (no content in source)\n", dest_id);
                delete_triple(db, dest_id, "inv:hasContent");
                side_effect = 1;
            }
        } else {
            LOG_WARN("⚠️ No matching SourcePlace found for DestinationPlace name: %s\n", dest_name);
        }

        sqlite3_finalize(src_stmt);
    }

    sqlite3_finalize(stmt);
    return side_effect;
}


#define MAX_ITERATIONS 3 // Or whatever feels safe for your app

int eval(sqlite3* db, const char* block) {
    LOG_INFO("⚙️  Starting eval() pass for %s (until stabilization)\n", block);
    int total_side_effects = 0;
    int iteration = 0;

    while (iteration < MAX_ITERATIONS) {
        int side_effect_this_round = 0;

        sqlite3_stmt* stmt;
        const char* sql = "SELECT subject FROM triples WHERE predicate = 'rdf:type' AND object = 'inv:Definition' AND psi = ?";

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
            LOG_ERROR("❌ Failed to prepare eval query: %s\n", sqlite3_errmsg(db));
            return total_side_effects;
        }

        sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
        LOG_INFO("🔁 Eval iteration #%d\n", ++iteration);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* expr_id = (const char*)sqlite3_column_text(stmt, 0);
            LOG_INFO("📘 Evaluating expression: %s\n", expr_id);

            if (cycle(db, block, expr_id)) {
                side_effect_this_round = 1;
                total_side_effects = 1;
            }

            eval_expression_literal_only(db, block, expr_id);
        }

        sqlite3_finalize(stmt);

        if (!side_effect_this_round) {
            LOG_INFO("✅ Eval for %s stabilized after %d iteration(s)\n", block, iteration);
            break;
        }
    }

    if (iteration >= MAX_ITERATIONS) {
        LOG_WARN("⚠️ Eval for %s did not stabilize after %d iterations. Bailout triggered.\n", block, MAX_ITERATIONS);
    }

    return total_side_effects;
}




