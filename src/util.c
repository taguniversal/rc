#include <stddef.h> // for size_t and NULL
#include <stdio.h>
#include <stdlib.h> // for malloc
#include <string.h> // for strchr, strlen, strncpy
#include "sqlite3.h"
#include "log.h"


char *prefix_name(const char *def_name, const char *signal_name) {
  char *prefixed =
      malloc(strlen(def_name) + strlen(signal_name) + 2); // ':' + '\0'
  sprintf(prefixed, "%s:%s", def_name, signal_name);
  return prefixed;
}

const char *last_component(const char *qualified) {
  const char *colon = strrchr(qualified, ':');
  return colon ? colon + 1 : qualified;
}


char *extract_left_of_colon(const char *str) {
  if (!str)
    return NULL;

  const char *colon = strchr(str, ':');
  size_t len = colon ? (size_t)(colon - str) : strlen(str);

  char *left = malloc(len + 1);
  if (!left)
    return NULL;

  strncpy(left, str, len);
  left[len] = '\0';

  return left;
}

void db_state(sqlite3 *db, const char *block) {
  sqlite3_stmt *stmt;
  const char *sql =
      "SELECT subject, object FROM triples "
      "WHERE predicate = 'inv:hasContent' AND psi = ? "
      "ORDER BY subject;";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      LOG_ERROR("❌ db_state: Failed to prepare statement\n");
      return;
  }

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);

  char line[129] = {0}; // 128 bits + null terminator
  int i = 0;

  while (sqlite3_step(stmt) == SQLITE_ROW && i < 128) {
      const unsigned char *value = sqlite3_column_text(stmt, 1);
      line[i++] = (value && strcmp((const char *)value, "1") == 0) ? '1' : '0';
  }

  sqlite3_finalize(stmt);

  LOG_INFO("🧬 Seed Row (CA:0..127):\n%s\n", line);
}

