#include "wiring.h"
#include "log.h"
#include "rdf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void dump_wiring(sqlite3 *db, const char *block) {
  LOG_INFO("🐐 GOAT Diagram: Signal Wiring");

  const char *sql_invocations =
    "SELECT DISTINCT subject FROM triples "
    "WHERE psi = ? AND predicate = 'rdf:type' AND object = 'inv:Invocation';";

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, sql_invocations, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ Failed to prepare invocation scan.");
    return;
  }
  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);

  char *buffer = strdup("");
  int any_printed = 0;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *expr_id = (const char *)sqlite3_column_text(stmt, 0);
    char *iname = lookup_object(db, block, expr_id, "inv:name");

    char short_uuid[7] = {0};
    const char *uuid_start = strstr(expr_id, "<:") ? expr_id + 2 : expr_id;
    strncpy(short_uuid, uuid_start, 6);

    char label[128];
    snprintf(label, sizeof(label), "[::%s] %s", short_uuid, iname ? iname : "(unnamed)");

    // === SourceList
    const char *sql_src = "SELECT object FROM triples WHERE psi = ? AND subject = ? AND predicate = 'inv:SourceList';";
    sqlite3_stmt *src_stmt;
    sqlite3_prepare_v2(db, sql_src, -1, &src_stmt, NULL);
    sqlite3_bind_text(src_stmt, 1, block, -1, SQLITE_STATIC);
    sqlite3_bind_text(src_stmt, 2, expr_id, -1, SQLITE_STATIC);

    char input_names[512] = {0};
    while (sqlite3_step(src_stmt) == SQLITE_ROW) {
      const char *src_id = (const char *)sqlite3_column_text(src_stmt, 0);
      char *src_name = lookup_object(db, block, src_id, "inv:name");
      char *val = lookup_object(db, block, src_id, "inv:hasContent");

      strncat(input_names, src_name ? src_name : src_id, sizeof(input_names) - strlen(input_names) - 1);
      strncat(input_names, "=", sizeof(input_names) - strlen(input_names) - 1);
      strncat(input_names, val ? val : "", sizeof(input_names) - strlen(input_names) - 1);
      strncat(input_names, val ? "✅ " : "🛸 ", sizeof(input_names) - strlen(input_names) - 1);

      if (src_name) free(src_name);
      if (val) free(val);
    }
    sqlite3_finalize(src_stmt);

    // === DestinationList
    const char *sql_dst = "SELECT object FROM triples WHERE psi = ? AND subject = ? AND predicate = 'inv:DestinationList';";
    sqlite3_stmt *dst_stmt;
    sqlite3_prepare_v2(db, sql_dst, -1, &dst_stmt, NULL);
    sqlite3_bind_text(dst_stmt, 1, block, -1, SQLITE_STATIC);
    sqlite3_bind_text(dst_stmt, 2, expr_id, -1, SQLITE_STATIC);

    char output_names[256] = {0};
    while (sqlite3_step(dst_stmt) == SQLITE_ROW) {
      const char *dst_id = (const char *)sqlite3_column_text(dst_stmt, 0);
      char *dst_name = lookup_object(db, block, dst_id, "inv:name");
      strncat(output_names, dst_name ? dst_name : dst_id, sizeof(output_names) - strlen(output_names) - 1);
      strncat(output_names, " ", sizeof(output_names) - strlen(output_names) - 1);
      if (dst_name) free(dst_name);
    }
    sqlite3_finalize(dst_stmt);

    // Print line
    char *line;
    asprintf(&line, "%s ⬅─ %s→ %s\n",
             label,
             input_names[0] ? input_names : "(no inputs)",
             output_names[0] ? output_names : "(no outputs)");

    buffer = realloc(buffer, strlen(buffer) + strlen(line) + 2);
    strcat(buffer, line);
    free(line);

    if (iname) free(iname);
    any_printed = 1;
  }

  sqlite3_finalize(stmt);

  if (any_printed) {
    LOG_INFO("\n%s", buffer);
  } else {
    LOG_INFO("⚠️  No invocations found in block '%s'.", block);
  }

  free(buffer);
  LOG_INFO("🟢 Done.");
}



