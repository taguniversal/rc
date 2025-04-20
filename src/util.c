#include <stddef.h> // for size_t and NULL
#include <stdio.h>
#include <stdlib.h> // for malloc
#include <string.h> // for strchr, strlen, strncpy
#include "sqlite3.h"
#include "log.h"
#include "cJSON.h"


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

int validate_json_structure(cJSON *root) {
  LOG_INFO("Starting validation\n");

  if (!root) {
    fprintf(stderr, "❌ Error: null JSON root.\n");
    return 1;
  }

  cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "@type");
  if (!type || !cJSON_IsString(type)) {
    fprintf(stderr, "❌ Error: Missing or invalid '@type'.\n");
    return 1;
  }

  const char *tval = type->valuestring;

  // Validate inv:Invocation
  if (strcmp(tval, "inv:Invocation") == 0) {
    cJSON *io = cJSON_GetObjectItemCaseSensitive(root, "inv:IO");
    if (!io || !cJSON_IsObject(io)) {
      fprintf(stderr, "❌ Error: 'inv:Invocation' missing 'inv:IO'.\n");
      return 1;
    }

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
    return 0;
  }

  // Validate inv:Definition
  if (strcmp(tval, "inv:Definition") == 0) {
    cJSON *io = cJSON_GetObjectItemCaseSensitive(root, "inv:IO");
    if (!io || !cJSON_IsObject(io)) {
      fprintf(stderr, "❌ Error: 'inv:Definition' missing 'inv:IO'.\n");
      return 1;
    }

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

    // Must contain *either* ConditionalInvocation *or* PlaceOfResolution
    cJSON *table = cJSON_GetObjectItemCaseSensitive(root, "inv:ConditionalInvocation");
    cJSON *por = cJSON_GetObjectItemCaseSensitive(root, "inv:PlaceOfResolution");

    if (!table && !por) {
      fprintf(stderr, "❌ Error: Definition must have 'inv:ConditionalInvocation' or 'inv:PlaceOfResolution'.\n");
      return 1;
    }

    // If ConditionalInvocation is present, validate keys are 0/1 combos (optional strictness)
    if (table && !cJSON_IsObject(table)) {
      fprintf(stderr, "❌ Error: 'inv:ConditionalInvocation' must be an object.\n");
      return 1;
    }

    // If PlaceOfResolution is present, ensure it contains hasExpressionFragment array
    if (por) {
      cJSON *frags = cJSON_GetObjectItemCaseSensitive(por, "inv:hasExpressionFragment");
      if (!frags || !cJSON_IsArray(frags)) {
        fprintf(stderr, "❌ Error: 'inv:PlaceOfResolution' missing 'inv:hasExpressionFragment'.\n");
        return 1;
      }
    }
    LOG_INFO("✅ Finished validation.. OK");
    return 0;
  }

  // Unknown type
  fprintf(stderr, "⚠️ Unknown '@type': %s — skipping strict validation.\n", tval);
  return 0;
}
