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

#include "cJSON.h"
#include <stdio.h>
#include <string.h>

// Returns 0 if valid, 1 if invalid
int validate_json_structure(cJSON *root) {
  if (!root) {
    fprintf(stderr, "❌ Error: null JSON root.\n");
    return 1;
  }

  // Check for @type
  cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "@type");
  if (!type || !cJSON_IsString(type)) {
    fprintf(stderr, "❌ Error: Missing or invalid '@type'.\n");
    return 1;
  }

  // Only validate inv:Definition or inv:Invocation
  if (strcmp(type->valuestring, "inv:Definition") != 0 &&
      strcmp(type->valuestring, "inv:Invocation") != 0) {
    // Not an IO-bearing construct, so skip validation
    return 0;
  }

  // Check for inv:IO
  cJSON *io = cJSON_GetObjectItemCaseSensitive(root, "inv:IO");
  if (!io || !cJSON_IsObject(io)) {
    fprintf(stderr, "❌ Error: Missing or invalid 'inv:IO' object.\n");
    return 1;
  }

  // Check for inputs and outputs
  cJSON *inputs = cJSON_GetObjectItemCaseSensitive(io, "inputs");
  cJSON *outputs = cJSON_GetObjectItemCaseSensitive(io, "outputs");

  if (!inputs || !cJSON_IsArray(inputs)) {
    fprintf(stderr, "❌ Error: 'inv:IO.inputs' is missing or not an array.\n");
    return 1;
  }

  if (!outputs || !cJSON_IsArray(outputs)) {
    fprintf(stderr, "❌ Error: 'inv:IO.outputs' is missing or not an array.\n");
    return 1;
  }

  // Passed all checks
  return 0;
}



