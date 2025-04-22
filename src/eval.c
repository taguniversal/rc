#include "log.h"
#include "rdf.h"
#include "sqlite3.h"
#include "util.h"
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>  // fprintf, stderr
#include <stdlib.h> // general utilities (optional but common)
#include <string.h>
#include <time.h>
#include <time.h>   // time_t, localtime, strftime
#include <unistd.h> // isatty, fileno

#define MAX_ITERATIONS 5 // Or whatever feels safe for your app

int is_place_null(sqlite3 *db, const char *block, const char *place_id) {
  return !has_predicate(db, block, place_id, "inv:hasContent");
}

/**
 * Checks whether an Invocation's input or output boundary is complete.
 * For a given direction ("inv:Input" or "inv:Output"), this verifies that all
 * required signal places (defined by the corresponding Definition) have
 * content.
 */
int check_invocation_boundary_complete(
    sqlite3 *db, const char *block, const char *expr_id,
    const char *direction // must be "inv:Input" or "inv:Output"
) {
  LOG_INFO("🔍 Checking boundary completeness for %s (%s)\n", expr_id,
           direction);

  // Validate direction
  if (strcmp(direction, "inv:Input") != 0 &&
      strcmp(direction, "inv:Output") != 0) {
    LOG_ERROR("❌ Invalid direction parameter: %s. Must be 'inv:Input' or "
              "'inv:Output'\n",
              direction);
    return -1;
  }

  // Ensure expr_id is an inv:Invocation
  char *type = lookup_object(db, block, expr_id, "rdf:type");
  if (!type || strcmp(type, "inv:Invocation") != 0) {
    LOG_ERROR("❌ %s is not an inv:Invocation (type = %s)\n", expr_id,
              type ? type : "(null)");
    free(type);
    return -1;
  }
  free(type);

  // Get the invocation name (used to find the matching Definition)
  char *def_name = lookup_object(db, block, expr_id, "inv:name");
  if (!def_name) {
    LOG_ERROR("❌ Could not find inv:name for invocation %s\n", expr_id);
    return -1;
  }

  // Translate "inv:Input"/"inv:Output" to RDF predicate
  const char *predicate = strcmp(direction, "inv:Input") == 0
                              ? "inv:SourceList"
                              : "inv:DestinationList";

  // Get the appropriate list of signal IDs
  char list[32][128]; // or larger if needed
  int count =
      lookup_definition_io_list(db, block, def_name, predicate, list, 32);

  if (count < 0) {
    LOG_ERROR("❌ Failed to retrieve I/O list from definition\n");
    free(def_name);
    return -1;
  }

  LOG_INFO("📦 Checking %s boundary on %s: %d signal(s)", direction, def_name,
           count);
  for (int i = 0; i < count; ++i) {
    LOG_INFO("   • %s", list[i]);
  }

  free(def_name);

  int all_present = 1;
  for (int i = 0; i < count; ++i) {
    const char *place = list[i];
    char *content = lookup_object(db, block, place, "inv:hasContent");

    if (!content) {
      LOG_INFO("🕳️  Missing content at %s → NOT COMPLETE\n", place);
      all_present = 0;
    } else {
      LOG_INFO("✅ Place %s has content: %s\n", place, content);
      free(content);
    }
  }

  return all_present;
}

/**
 * Expands a dynamic invocation string into a concrete resolution key.
 * This function replaces each variable (e.g., "$A") in the template with the
 * current content value of its scoped signal (e.g., DEF:A), forming a key used
 * to index into a definition's resolution table.
 */
char *build_resolution_key(sqlite3 *db, const char *block,
                           const char *definition_name,
                           const char *invocation_str) {
  if (!invocation_str)
    return NULL;

  char *inv_str_copy = strdup(invocation_str);
  const char *p = inv_str_copy;

  LOG_INFO("BEGIN build_resolution_key def_name: %s invocation_str: %s (%p)\n",
           definition_name, inv_str_copy, (void *)invocation_str);

  char key[256] = {0};

  while (*p) {
    if (*p == '$') {
      p++; // skip '$'
      char var[64] = {0};
      int vi = 0;
      while (*p && isalnum(*p)) {
        var[vi++] = *p++;
      }
      var[vi] = '\0';

      char *scoped_var_id = prefix_name(definition_name, var);
      char *val = lookup_object(db, block, scoped_var_id, "inv:hasContent");

      // 🔁 If val is a scoped ID (e.g., NOT:Out0), resolve it further
      if (val && strchr(val, ':')) {
        char *indirect = lookup_object(db, block, val, "inv:hasContent");
        if (indirect) {
          free(val);
          val = indirect;
        }
      }

      LOG_INFO("🔍 Building key — resolving var: %s", var);
      LOG_INFO("     var_id = %s", scoped_var_id ? scoped_var_id : "(null)");
      LOG_INFO("     val    = %s", val ? val : "(null)");

      if (val) {
        strncat(key, val, sizeof(key) - strlen(key) - 1);
      } else {
        strncat(key, "_", sizeof(key) - strlen(key) - 1);
      }

      free(scoped_var_id);
      free(val); // Always free val, which might be original or indirect
    } else {
      LOG_INFO("⏭️ Skipping non-$ char: %c", *p);
      p++;
    }
  }

  LOG_INFO("END build_resolution_key invocation_str: %s\n", inv_str_copy);
  LOG_INFO("🧪 Raw template: '%s' (len=%zu)", inv_str_copy,
           strlen(inv_str_copy));
  for (size_t i = 0; i < strlen(inv_str_copy); ++i) {
    LOG_INFO("char[%zu] = '%c' (0x%02X)", i, inv_str_copy[i],
             (unsigned char)inv_str_copy[i]);
  }

  LOG_INFO("build_resolution_key built key: %s\n", key);
  free(inv_str_copy);
  return strdup(key);
}

int resolve_conditional_invocation(sqlite3 *db, const char *block,
                                   const char *def_id, const char *expr_id) {

  if (!check_invocation_boundary_complete(db, block, expr_id, "inv:Input")) {
    LOG_INFO("⛔ Input boundary not complete — skipping resolution for %s\n",
             expr_id);
    return 0;
  }

  int side_effect = 0;
  LOG_INFO("🧠 Attempting resolution from ConditionalInvocation in %s\n",
           expr_id);

  char *por = lookup_object(db, block, expr_id, "inv:PlaceOfResolution");
  if (!por)
    return 0;

  char *frag = lookup_object(db, block, por, "inv:hasExpressionFragment");
  if (!frag) {
    free(por);
    return 0;
  }

  char *cond = lookup_object(db, block, frag, "inv:ConditionalInvocation");
  if (!cond) {
    free(por);
    free(frag);
    return 0;
  }
  LOG_INFO("Resolving resolution key for conditional invocaton %s\n", cond);

  char *template = lookup_object(db, block, cond, "inv:invocationName");
  if (!template) {
    LOG_WARN("⚠️ Missing invocationName template in ConditionalInvocation\n");
    free(por);
    free(frag);
    free(cond);
    return 0;
  }

  LOG_INFO("🧪 Raw template: '%s' (len=%zu)", template, strlen(template));
  LOG_INFO("Calling build_resolution_key with: %s (%p)\n", template,
           (void *)template);
  char *key = build_resolution_key(db, block, expr_id, template);
  if (!key) {
    LOG_WARN("⚠️ Could not build resolution key from template: %s\n", template);
    free(por);
    free(frag);
    free(cond);
    free(template);
    return 0;
  }

  LOG_INFO("🧩 Built resolution key from template: %s → %s\n", template, key);

  char *output_name = lookup_object(db, block, cond, "inv:Output");
  if (!output_name) {
    LOG_WARN("⚠️ No Output defined in ConditionalInvocation\n");
    free(por);
    free(frag);
    free(cond);
    free(template);
    free(key);
    return 0;
  }

  char *output_id = lookup_subject_by_name(db, block, output_name);
  if (!output_id) {
    LOG_WARN("⚠️ Could not resolve output name to ID: %s\n", output_name);
    free(por);
    free(frag);
    free(cond);
    free(template);
    free(key);
    return 0;
  }

  char *contained_def_id =
      lookup_object(db, block, expr_id, "inv:ContainsDefinition");
  if (!def_id) {
    LOG_WARN("⚠️ Missing ContainsDefinition in %s\n", expr_id);
    free(output_id);
    free(por);
    free(frag);
    free(cond);
    free(template);
    free(key);
    return 0;
  }

  char *result = lookup_object(db, block, contained_def_id, key);
  if (!result) {
    LOG_WARN("❌ Resolution table has no entry for key: %s\n", key);
    free(output_id);
    free(por);
    free(frag);
    free(cond);
    free(template);
    free(key);
    return 0;
  }

  char *existing = lookup_object(db, block, output_id, "inv:hasContent");
  if (!existing || strcmp(existing, result) != 0) {
    LOG_INFO("📥 resolve_conditional - writing resolution result %s into %s\n",
             result, output_id);
    insert_triple(db, block, output_id, "inv:hasContent", result);
    side_effect = 1;
  }
  free(existing);

  free(result);
  free(output_id);
  free(por);
  free(frag);
  free(cond);
  free(template);
  free(key);
  return side_effect;
}

/**
 * Evaluates a ConditionalInvocation within an Invocation context.
 * If all inputs are bound, this function computes a resolution key from the
 * invocation template (e.g., "$A$B"), looks it up in the associated resolution
 * table, and writes the resulting value to the specified output signal.
 */
int resolve_definition_output(sqlite3 *db, const char *block,
                              const char *definition_id) {
  int side_effect = 0;
  LOG_INFO("resolve_definition_output for %s\n", definition_id);

  char *type = lookup_object(db, block, definition_id, "rdf:type");
  LOG_INFO("📎 Found rdf:type = [%s] for definition [%s]\n",
           type ? type : "(null)", definition_id ? definition_id : "(null)");

  if (!type || (strcmp(type, "inv:Definition") != 0 &&
                strcmp(type, "inv:ContainedDefinition") != 0)) {
    LOG_WARN("No valid type found for %s — got %s\n", definition_id,
             type ? type : "(null)");
    free(type);
    return 0;
  }

  char *por = lookup_object(db, block, definition_id, "inv:PlaceOfResolution");
  if (!por) {
    LOG_WARN("No Place of Resolution found\n");
    free(type);
    return 0;
  }

  char *frag = lookup_object(db, block, por, "inv:hasExpressionFragment");
  if (!frag) {
    LOG_WARN("No Expression fragment found\n");
    free(type);
    free(por);
    return 0;
  }

  // Check if a ConditionalInvocation is present at all
  sqlite3_stmt *check_stmt;
  const char *check_sql = "SELECT 1 FROM triples WHERE psi = ? AND subject = ? "
                          "AND predicate = 'inv:ConditionalInvocation' LIMIT 1";

  if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ Failed to prepare ConditionalInvocation existence check\n");
    free(type);
    free(por);
    free(frag);
    return 0;
  }

  sqlite3_bind_text(check_stmt, 1, block, -1, SQLITE_STATIC);
  sqlite3_bind_text(check_stmt, 2, frag, -1, SQLITE_STATIC);

  int has_conditional = sqlite3_step(check_stmt) == SQLITE_ROW;
  sqlite3_finalize(check_stmt);

  if (!has_conditional) {
    LOG_INFO(
        "ℹ️ No ConditionalInvocation present — skipping resolution output\n");
    free(type);
    free(por);
    free(frag);
    return 0;
  }

  char *cond = lookup_object(db, block, frag, "inv:ConditionalInvocation");
  if (!cond) {
    LOG_WARN("⚠️ Missing conditional invocation for frag %s\n", frag);
    free(type);
    free(por);
    free(frag);
    return 0;
  }

  LOG_INFO("🧠 Attempting resolution from ConditionalInvocation in %s\n",
           definition_id);
  LOG_INFO("📎 Fetched ConditionalInvocation: %s\n", cond);

  char *invocation_str =
      lookup_object(db, block, cond, "inv:ResolvedInvocationName");
  if (!invocation_str) {
    invocation_str = lookup_object(db, block, cond, "inv:invocationName");
    LOG_INFO("🧩 Fallback to template name: %s\n",
             invocation_str ? invocation_str : "(null)");
  }

  char *output_name =
      lookup_object(db, block, cond, "inv:Output"); // ← FIXED: now a char *
  if (!invocation_str || !output_name) {
    LOG_WARN("⚠️ Missing required fields in ConditionalInvocation %s\n", cond);
    free(type);
    free(por);
    free(frag);
    free(cond);
    free(invocation_str);
    free(output_name);
    return 0;
  }

  LOG_INFO("🔧 cond = %s, output_name = %s\n", cond, output_name);

  char *output_id = lookup_subject_by_name(db, block, output_name);
  if (!output_id) {
    LOG_WARN("⚠️ Could not resolve subject for output name: %s\n", output_name);
    free(type);
    free(por);
    free(frag);
    free(cond);
    free(invocation_str);
    free(output_name);
    return 0;
  }
  // definition_id: OR:def0
  char *key = build_resolution_key(
      db, block, extract_left_of_colon(definition_id), invocation_str);
  if (!key) {
    LOG_WARN("⚠️ Failed to build resolution key from: %s\n", invocation_str);
    free(type);
    free(por);
    free(frag);
    free(cond);
    free(invocation_str);
    free(output_name);
    free(output_id);
    return 0;
  }

  LOG_INFO("🧪 Resolution lookup key: %s\n", key);

  char *def_node =
      lookup_object(db, block, definition_id, "inv:ContainsDefinition");
  char *result = lookup_object(db, block, def_node, key);

  if (!result) {
    LOG_WARN("⚠️ Resolution table missing entry for key: %s\n", key);
    free(type);
    free(por);
    free(frag);
    free(cond);
    free(invocation_str);
    free(output_name);
    free(output_id);
    free(def_node);
    free(key);
    return 0;
  }

  char *existing = lookup_object(db, block, output_id, "inv:hasContent");
  if (!existing || strcmp(existing, result) != 0) {
    LOG_INFO("📥 Inserting resolution result %s into %s\n", result, output_id);
    insert_triple(db, block, output_id, "inv:hasContent", result);
    side_effect = 1;
  }
  free(existing);

  // Clean up everything
  free(type);
  free(por);
  free(frag);
  free(cond);
  free(invocation_str);
  free(output_name);
  free(output_id);
  free(def_node);
  free(key);
  free(result);
  return side_effect;
}

/**
 * Retrieves the raw input value at a given index from an Invocation's IO array.
 * This reads the JSON-encoded inv:Inputs list attached to the invocation,
 * and returns the string at the specified position (e.g., "1", "$A", etc).
 */
const char *get_input_value_from_invocation(sqlite3 *db, const char *expr_id,
                                            int index) {
  sqlite3_stmt *stmt;
  const char *sql = "SELECT object FROM triples WHERE subject = ? AND "
                    "predicate = 'inv:Inputs'";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return NULL;

  sqlite3_bind_text(stmt, 1, expr_id, -1, SQLITE_STATIC);

  const char *array_json = NULL;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    array_json = strdup((const char *)sqlite3_column_text(stmt, 0));
  }

  sqlite3_finalize(stmt);

  if (!array_json)
    return NULL;

  cJSON *arr = cJSON_Parse(array_json);
  free((void *)array_json);
  if (!arr || !cJSON_IsArray(arr))
    return NULL;

  cJSON *val = cJSON_GetArrayItem(arr, index);
  if (!val || !cJSON_IsString(val)) {
    cJSON_Delete(arr);
    return NULL;
  }

  const char *result = strdup(val->valuestring);
  cJSON_Delete(arr);
  return result;
}

/**
 * Binds raw input values to the scoped input signals of an Invocation.
 * Matches the input list in inv:Inputs (JSON) with formal parameter names
 * from the Definition’s inv:IO block, storing resolved values in the RDF graph.
 * Returns 1 if any binding caused a state change, 0 if all inputs were already
 * bound.
 */
int bind_inputs(sqlite3 *db, const char *block, const char *expr_id) {
  LOG_INFO("🔗 Binding inputs for expression: %s\n", expr_id);
  char *inv_name = lookup_object(db, block, expr_id, "inv:name");
  LOG_INFO("🔗 Binding inputs for [%s] (name = %s)", expr_id,
           inv_name ? inv_name : "null");
  free(inv_name);

  char *type = lookup_object(db, block, expr_id, "rdf:type");
  if (!type || strcmp(type, "inv:Invocation") != 0) {
    LOG_WARN("⚠️ Unexpected or missing expression type for: %s\n", expr_id);
    if (type)
      free(type);
    return 0;
  }

  char *inv_id = strdup(expr_id);
  char *def_name = lookup_object(db, block, inv_id, "inv:name");
  char *input_vals_json = lookup_raw_value(db, block, inv_id, "inv:Inputs");

  if (!def_name || !input_vals_json) {
    LOG_WARN("⚠️ Invocation %s missing name or Inputs\n", inv_id);
    free(type);
    free(def_name);
    free(inv_id);
    free(input_vals_json);
    return 0;
  }

  // Lookup definition node by matching inv:name
  char *def_id = lookup_subject_by_object(db, block, "inv:name", def_name);
  if (!def_id) {
    LOG_ERROR("❌ Could not find definition for name: %s\n", def_name);
    free(type);
    free(def_name);
    free(inv_id);
    free(input_vals_json);
    return 0;
  }

  // Lookup inv:IO on the definition to get formal param names
  char *io_json = lookup_raw_value(db, block, def_id, "inv:IO");
  if (!io_json) {
    LOG_ERROR("❌ Missing inv:IO on definition: %s\n", def_id);
    free(type);
    free(def_name);
    free(def_id);
    free(inv_id);
    free(input_vals_json);
    return 0;
  }

  cJSON *input_vals = cJSON_Parse(input_vals_json);
  cJSON *io_obj = cJSON_Parse(io_json);
  cJSON *input_keys = cJSON_GetObjectItem(io_obj, "inputs");

  if (!cJSON_IsArray(input_vals) || !cJSON_IsArray(input_keys)) {
    LOG_ERROR("❌ Malformed input arrays in invocation or definition\n");
    cJSON_Delete(input_vals);
    cJSON_Delete(io_obj);
    free(type);
    free(def_name);
    free(def_id);
    free(inv_id);
    free(input_vals_json);
    free(io_json);
    return 0;
  }

  LOG_INFO("👉 Binding for invocation = %s (definition = %s)\n", inv_id,
           def_name);
  LOG_INFO("🔢 Input values JSON: %s\n", input_vals_json);

  int side_effect = 0;
  int len = cJSON_GetArraySize(input_keys);
  for (int i = 0; i < len; i++) {
    cJSON *key = cJSON_GetArrayItem(input_keys, i);
    cJSON *val = cJSON_GetArrayItem(input_vals, i);
    if (!key || !val || !cJSON_IsString(key) || !cJSON_IsString(val)) {
      continue;
    }

    const char *name = key->valuestring;
    const char *value = val->valuestring;

    char scoped_id[256];
    snprintf(scoped_id, sizeof(scoped_id), "%s:%s", inv_id, name);

    char *existing = lookup_object(db, block, scoped_id, "inv:hasContent");

    if (!existing || strcmp(existing, value) != 0) {
      LOG_INFO("📅 Binding [%s] = %s\n", scoped_id, value);
      insert_triple(db, block, scoped_id, "inv:hasContent", value);
      side_effect = 1;
    } else {
      LOG_INFO("⚖️  Skipping bind: [%s] already has content = %s\n", scoped_id,
               existing);
    }

    if (existing)
      free(existing);
  }

  cJSON_Delete(input_vals);
  cJSON_Delete(io_obj);
  free(type);
  free(def_name);
  free(def_id);
  free(inv_id);
  free(input_vals_json);
  free(io_json);
  return side_effect;
}

bool is_contained_definition(sqlite3 *db, const char *block,
                             const char *expr_id) {
  char *type = lookup_object(db, block, expr_id, "rdf:type");
  if (!type)
    return false;

  bool result = (strcmp(type, "inv:ContainedDefinition") == 0);
  free(type);
  return result;
}

/**
 * Evaluates all invocations currently marked as `inv:Executing`.
 * For each, attempts to resolve its output using the definition’s resolution
 * table. If the output is stable (no state change), the Executing flag is
 * cleared.
 */
void check_executing_invocations(sqlite3 *db, const char *block) {
  LOG_INFO("🧪 Checking all invocations marked Executing...\n");

  sqlite3_stmt *stmt;
  const char *sql =
      "SELECT subject FROM triples "
      "WHERE predicate = 'inv:Executing' AND psi = ? "
      "AND subject IN ("
      "  SELECT subject FROM triples "
      "  WHERE predicate = 'rdf:type' AND object = 'inv:Invocation' AND psi = ?"
      ");";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ Failed to prepare executing invocation query: %s\n",
              sqlite3_errmsg(db));
    return;
  }

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC); // for Executing
  sqlite3_bind_text(stmt, 2, block, -1, SQLITE_STATIC); // for rdf:type check

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *inv_id = (const char *)sqlite3_column_text(stmt, 0);
    LOG_INFO("🔬 Running resolve_definition_output on %s...\n", inv_id);

    int side_effect = resolve_definition_output(db, block, inv_id);
    if (!side_effect) {
      LOG_INFO("✅ %s output stable, clearing Executing flag\n", inv_id);
      delete_triple(db, block, inv_id, "inv:Executing");
    } else {
      LOG_INFO("🔁 %s still evolving, keeping Executing\n", inv_id);
    }
  }

  sqlite3_finalize(stmt);
}

/**
 * Resolves outputs from a Definition to the corresponding Invocation.
 * For each output defined in the Definition's `inv:IO.outputs`, this function:
 *   - Looks up the value in the Definition scope,
 *   - Maps it positionally to the Invocation’s DestinationList,
 *   - Copies the content if it differs from the existing value.
 *
 * This models the output propagation stage where a Definition’s internal
 * results are exposed to the outer world through the Invocation’s named output
 * ports.
 */
int resolve_invocation_outputs(sqlite3 *db, const char *block,
                               const char *invocation_uuid) {
  int side_effect = 0;
  LOG_INFO("resollve_invocation_outputs for invocation uuid %s\n",
           invocation_uuid);

  // Lookup definition name
  char *def_id = lookup_object(db, block, invocation_uuid, "inv:ofDefinition");
  if (!def_id) {
    LOG_WARN("⚠️ Invocation %s missing inv:ofDefinition\n", invocation_uuid);
    return 0;
  }

  char *def_subject = lookup_subject_by_object(db, block, "inv:name", def_id);
  if (!def_subject) {
    LOG_WARN("⚠️ Could not resolve definition subject for %s\n", def_id);
    free(def_id);
    return 0;
  }

  char *io_json = lookup_raw_value(db, block, def_subject, "inv:IO");
  if (!io_json) {
    LOG_WARN("⚠️ No inv:IO found for definition %s\n", def_subject);
    free(def_subject);
    free(def_id);
    return 0;
  }

  cJSON *io = cJSON_Parse(io_json);
  cJSON *outputs = cJSON_GetObjectItem(io, "outputs");

  if (!cJSON_IsArray(outputs)) {
    LOG_WARN("⚠️ inv:IO.outputs missing or invalid for %s\n", def_subject);
    cJSON_Delete(io);
    free(io_json);
    free(def_subject);
    free(def_id);
    return 0;
  }

  sqlite3_stmt *stmt;
  const char *sql = "SELECT object FROM triples WHERE psi = ? AND subject = ? "
                    "AND predicate = 'inv:DestinationList'";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ Failed to prepare destination list query.");
    cJSON_Delete(io);
    free(io_json);
    free(def_subject);
    free(def_id);
    return 0;
  }

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, invocation_uuid, -1, SQLITE_STATIC);

  int idx = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *inv_dest = (const char *)sqlite3_column_text(stmt, 0);
    cJSON *def_out = cJSON_GetArrayItem(outputs, idx++);
    if (!def_out || !cJSON_IsString(def_out))
      continue;

    char *scoped_def_out = prefix_name(def_subject, def_out->valuestring);
    char *val = lookup_object(db, block, scoped_def_out, "inv:hasContent");
    LOG_INFO("🔁 Mapping def output %s (%s) → invocation dest %s\n",
             def_out->valuestring, scoped_def_out, inv_dest);

    if (val) {
      char *existing = lookup_object(db, block, inv_dest, "inv:hasContent");
      if (!existing || strcmp(existing, val) != 0) {
        insert_triple(db, block, inv_dest, "inv:hasContent", val);
        LOG_INFO("📤 Output: %s → %s = %s\n", scoped_def_out, inv_dest, val);
        side_effect = 1;
      }
      free(existing);
    }

    free(val);
    free(scoped_def_out);
  }

  sqlite3_finalize(stmt);
  cJSON_Delete(io);
  free(io_json);
  free(def_subject);
  free(def_id);
  return side_effect;
}

/**
 * Checks if the Invocation has all its input signals present (ready).
 * If the readiness status has changed since the last evaluation, it updates
 * the `inv:WasReady` marker accordingly and returns a side-effect flag.
 * 
 * This ensures that readiness transitions (NULL→Value or Value→NULL) are tracked,
 * enabling the system to know when an Invocation becomes eligible (or ineligible)
 * for resolution.
 */
int check_readiness_and_mark_transitions(sqlite3 *db, const char *block,
                                         const char *uuid) {
  int is_ready_now =
      check_invocation_boundary_complete(db, block, uuid, "inv:Input");
  int was_ready = has_triple(db, block, uuid, "inv:WasReady", "true");

  if (!was_ready && is_ready_now) {
    LOG_INFO("🌊 NULL→Value transition: %s is now ready to flow\n", uuid);
    insert_triple(db, block, uuid, "inv:WasReady", "true");
    return 1;
  }

  if (was_ready && !is_ready_now) {
    LOG_INFO("💤 Value→NULL transition for invocation: %s no longer ready\n",
             uuid);
    delete_triple(db, block, uuid, "inv:WasReady");
    return 1;
  }

  if (!is_ready_now) {
    LOG_INFO("⛔ Not ready — skipping resolution for %s\n", uuid);
  }

  return 0;
}

/**
 * Resolves dynamic invocation names in a ConditionalInvocation block.
 * 
 * If a template string (e.g., "$A$B") is found in the ConditionalInvocation,
 * this function computes its resolved name based on the current input signal values
 * and inserts a `inv:ResolvedInvocationName` triple if it has changed.
 * 
 * This enables conditional expressions (like truth tables or pattern maps)
 * to dynamically route execution or data based on input signal states.
 */
int evaluate_conditional_invocation(sqlite3 *db, const char *block,
                                    const char *invocation_uuid) {
  int side_effect = 0;
  // Step 1.5: Evaluate ConditionalInvocation dynamic names
  char *por =
      lookup_object(db, block, invocation_uuid, "inv:PlaceOfResolution");
  if (por) {
    LOG_INFO("🪵 DEBUG: Found PlaceOfResolution: %s\n", por); // 🪵 DEBUG
    char *frag = lookup_object(db, block, por, "inv:hasExpressionFragment");
    if (frag) {
      LOG_INFO("🪵 DEBUG: Found ExpressionFragment: %s\n", frag); // 🪵 DEBUG

      // Check if a ConditionalInvocation exists at all
      sqlite3_stmt *check_stmt;
      const char *check_sql =
          "SELECT 1 FROM triples WHERE psi = ? AND subject = ? "
          "AND predicate = 'inv:ConditionalInvocation' LIMIT 1";

      if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, NULL) ==
          SQLITE_OK) {
        sqlite3_bind_text(check_stmt, 1, block, -1, SQLITE_STATIC);
        sqlite3_bind_text(check_stmt, 2, frag, -1, SQLITE_STATIC);

        int has_conditional = sqlite3_step(check_stmt) == SQLITE_ROW;
        sqlite3_finalize(check_stmt);

        if (has_conditional) {
          char *cond =
              lookup_object(db, block, frag, "inv:ConditionalInvocation");
          if (cond) {
            LOG_INFO("📍 Fetched ConditionalInvocation: %s\n", cond);
            char *template =
                lookup_object(db, block, cond, "inv:invocationName");
            if (template && strchr(template, '$')) {
              LOG_INFO("🔧 template from %s is: %s\n", cond, template);
              char *resolved =
                  build_resolution_key(db, block, invocation_uuid, template);
              if (resolved) {
                LOG_INFO("🧩 Resolved dynamic invocationName = %s\n", resolved);
                char *existing_inv = lookup_object(
                    db, block, cond, "inv:ResolvedInvocationName");
                if (!existing_inv || strcmp(existing_inv, resolved) != 0) {
                  LOG_INFO(
                      "🪵 DEBUG: inserting ResolvedInvocationName for %s\n",
                      cond);
                  insert_triple(db, block, cond, "inv:ResolvedInvocationName",
                                resolved);
                  side_effect = 1;
                } else {
                  LOG_INFO(
                      "🔁 ResolvedInvocationName already up-to-date for %s\n",
                      cond);
                }
                free(existing_inv);
                free(resolved);

              } else {
                LOG_WARN("⚠️ Failed to build invocationName for cond = %s\n",
                         cond);
              }
              free(template);
            } else {
              LOG_INFO("🪵 DEBUG: Static invocationName or missing: %s\n",
                       template ?: "(null)"); // 🪵 DEBUG
              free(template);
            }
            free(cond);
          }
        } else {
          LOG_INFO("ℹ️ No ConditionalInvocation present in frag %s\n", frag);
        }
      } else {
        LOG_ERROR(
            "❌ Failed to prepare ConditionalInvocation presence check.\n");
      }
      free(frag);
    }
    free(por);
  }
  return side_effect;
}

/**
 * Propagates values from SourcePlaces to their connected DestinationPlaces.
 * 
 * For each DestinationPlace attached to the invocation, this scans for a
 * SourcePlace with a matching name (across the block) and, if it has content,
 * copies that value forward. If the content is no longer present in the source,
 * the destination is cleared to reflect the disconnection.
 * 
 * This mechanism models digital signal propagation across invocation boundaries.
 */
int propagate_output_values(sqlite3 *db, const char *block, const char *uuid) {
  int side_effect = 0;
  sqlite3_stmt *stmt;
  const char *sql = "SELECT object FROM triples WHERE psi = ? AND subject = ? "
                    "AND predicate = 'inv:destination_list'";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("Failed to prepare destination_list query.");
    return 0;
  }

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, uuid, -1, SQLITE_STATIC);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *dest_id = (const char *)sqlite3_column_text(stmt, 0);
    char *dest_name = lookup_object(db, block, dest_id, "inv:name");
    if (!dest_name) {
      LOG_WARN("⚠️ DestinationPlace %s has no name\n", dest_id);
      continue;
    }

    sqlite3_stmt *src_stmt;
    const char *src_sql = "SELECT subject FROM triples WHERE psi = ? AND "
                          "predicate = 'inv:name' AND object = ?";
    if (sqlite3_prepare_v2(db, src_sql, -1, &src_stmt, NULL) != SQLITE_OK) {
      LOG_ERROR("Failed to prepare source match query.");
      free(dest_name);
      continue;
    }

    sqlite3_bind_text(src_stmt, 1, block, -1, SQLITE_STATIC);
    sqlite3_bind_text(src_stmt, 2, dest_name, -1, SQLITE_STATIC);

    if (sqlite3_step(src_stmt) == SQLITE_ROW) {
      const char *source_id = (const char *)sqlite3_column_text(src_stmt, 0);
      char *content = lookup_object(db, block, source_id, "inv:hasContent");

      if (content) {
        char *existing = lookup_object(db, block, dest_id, "inv:hasContent");

        if (!existing || strcmp(existing, content) != 0) {
          LOG_INFO("📤 %s → %s : %s\n", source_id, dest_id, content);
          insert_triple(db, block, dest_id, "inv:hasContent", content);
          side_effect = 1;
        } else {
          char *existing = lookup_object(db, block, dest_id, "inv:hasContent");
          if (existing) {
            LOG_INFO("🔁 Clearing %s (no content in source)\n", dest_id);
            delete_triple(db, block, dest_id, "inv:hasContent");
            side_effect = 1;
          }
          free(existing);
        }

        free(existing);
        free(content);
      }

    } else {
      LOG_WARN(
          "⚠️ No matching SourcePlace found for DestinationPlace name: %s\n",
          dest_name);
    }

    sqlite3_finalize(src_stmt);
    free(dest_name);
  }

  sqlite3_finalize(stmt);
  return side_effect;
}

/**
 * Evaluates the resolution output of an invocation's contained definition.
 * 
 * If the invocation includes a ContainedDefinition, this first checks for
 * a ConditionalInvocation to resolve dynamically. If not present or inactive,
 * it falls back to resolve the definition's output directly using the static
 * resolution table. Only outer definitions are processed; inner definitions
 * are skipped to avoid redundant evaluation.
 */
int evaluate_resolution_table(sqlite3 *db, const char *block,
                              const char *uuid) {
  int side_effect = 0;

  char *contained_def_id =
      lookup_object(db, block, uuid, "inv:ContainsDefinition");
  if (!contained_def_id)
    return 0;

  int resolved_conditional_invocation =
      resolve_conditional_invocation(db, block, contained_def_id, uuid);
  if (!resolved_conditional_invocation) {
    if (!is_contained_definition(db, block, contained_def_id)) {
      LOG_INFO("📎 Passing def_id into resolve_definition_output: %s\n",
               contained_def_id);
      int output_side_effect =
          resolve_definition_output(db, block, contained_def_id);
      LOG_INFO("🪵 DEBUG: resolve_definition_output returned %d\n",
               output_side_effect);
      side_effect |= output_side_effect;
    } else {
      LOG_INFO(
          "🔬 Skipping resolve_definition_output for ContainedDefinition: %s\n",
          contained_def_id);
    }
  }

  free(contained_def_id);
  return side_effect;
}

/**
 * Executes a single evaluation cycle for an invocation.
 *
 * A cycle attempts to bind all input values, then checks readiness across
 * the input boundary. If the invocation is ready (all inputs have values),
 * it proceeds to:
 *   1. Resolve any dynamic ConditionalInvocation templates.
 *   2. Propagate outputs to named destinations.
 *   3. Mirror values from the invoked Definition's outputs.
 *   4. Resolve entries in the resolution table (e.g. for conditional logic).
 *
 * If any step changes state (e.g. value inserted/removed), a side effect
 * is recorded. The cycle is part of the stabilization loop and will repeat
 * until all invocations in the block have reached steady state.
 */
int cycle(sqlite3 *db, const char *block, const char *invocation_uuid) {
  LOG_INFO("⚙️  Starting cycle() pass for block %s invocation uuid: %s", block,
           invocation_uuid);

  int side_effect = 0;

  if (bind_inputs(db, block, invocation_uuid)) {
    LOG_INFO("🔗 Input binding caused side effects.\n");
    side_effect = 1;
  } else {
    LOG_INFO("🔗 Input binding caused NO side effects.\n");
  }

  int is_ready_now = check_invocation_boundary_complete(
      db, block, invocation_uuid, "inv:Input");
  int was_ready =
      has_triple(db, block, invocation_uuid, "inv:WasReady", "true");

  if (!was_ready && is_ready_now) {
    LOG_INFO("🌊 NULL→Value transition: %s is now ready to flow\n",
             invocation_uuid);
    insert_triple(db, block, invocation_uuid, "inv:WasReady", "true");
    side_effect = 1;
  } else if (was_ready && !is_ready_now) {
    LOG_INFO("💤 Value→NULL transition for invocation: %s no longer ready\n",
             invocation_uuid);
    delete_triple(db, block, invocation_uuid, "inv:WasReady");
    side_effect = 1;
  }

  if (!is_ready_now) {
    LOG_INFO("⛔ Not ready — skipping resolution for %s\n", invocation_uuid);
    return side_effect;
  }

  side_effect |= evaluate_conditional_invocation(db, block, invocation_uuid);
  side_effect |= propagate_output_values(db, block, invocation_uuid);
  side_effect |= resolve_invocation_outputs(db, block, invocation_uuid);
  side_effect |= evaluate_resolution_table(db, block, invocation_uuid);

  check_executing_invocations(db, block);

  LOG_INFO("🔚 cycle(%s, %s) returned side_effect = %d\n", block,
           invocation_uuid, side_effect);

  return side_effect;
}

void log_seed(sqlite3 *db, const char *block) {
  char line[129] = {0}; // 128 bits + null terminator
  sqlite3_stmt *stmt;

  for (int i = 0; i < 128; i++) {
    char subject[64];
    snprintf(subject, sizeof(subject), "CA:%d", i);

    const char *sql = "SELECT object FROM triples WHERE subject = ? AND "
                      "predicate = 'inv:hasContent' AND psi = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      LOG_ERROR("❌ Failed to prepare seed query: %s\n", sqlite3_errmsg(db));
      return;
    }

    sqlite3_bind_text(stmt, 1, subject, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, block, -1, SQLITE_TRANSIENT);

    int bit = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      const unsigned char *content = sqlite3_column_text(stmt, 0);
      if (content && strcmp((const char *)content, "1") == 0) {
        bit = 1;
      }
    }

    sqlite3_finalize(stmt);
    line[i] = bit ? '1' : '0';
  }

  LOG_INFO("🧬 Seed Row (CA:0..127):\n%s\n", line);
}

/**
 * Performs a full evaluation pass on all invocations in the block.
 *
 * This function iterates over every `inv:Invocation` in the RDF graph and
 * calls `cycle()` repeatedly until no further side effects occur, meaning
 * all signals have stabilized. Each iteration may bind inputs, propagate
 * outputs, resolve conditional invocations, or update resolution tables.
 *
 * Terminates early upon stabilization, or forcefully after MAX_ITERATIONS
 * to prevent infinite loops in unstable or cyclic graphs.
 *
 * Returns the total number of state changes (side effects) across all iterations.
 */
int eval(sqlite3 *db, const char *block) {
  LOG_INFO("⚙️  Starting eval() pass for %s (until stabilization)\n", block);
  int total_side_effects = 0;
  int iteration = 0;

  while (iteration < MAX_ITERATIONS) {
    int side_effect_this_round = 0;

    sqlite3_stmt *stmt;
    const char *sql = "SELECT subject FROM triples WHERE predicate = "
                      "'rdf:type' AND object = 'inv:Invocation' AND psi = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      LOG_ERROR("❌ Failed to prepare eval query: %s\n", sqlite3_errmsg(db));
      return total_side_effects;
    }

    sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
    LOG_INFO("🔁 Eval iteration #%d\n", ++iteration);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char *invocation_name = (const char *)sqlite3_column_text(stmt, 0);
      LOG_INFO("📘 Evaluating invocation: %s\n", invocation_name);

      if (cycle(db, block, invocation_name)) {
        side_effect_this_round = 1;
        total_side_effects = total_side_effects + 1;
      }
    }

    sqlite3_finalize(stmt);

    if (!side_effect_this_round) {
      LOG_INFO("✅ Eval for %s stabilized after %d iteration(s)\n", block,
               iteration);
      log_seed(db, block);
      break;
    }
  }

  if (iteration >= MAX_ITERATIONS) {
    LOG_WARN("⚠️ Eval for %s did not stabilize after %d iterations. Bailout "
             "triggered.\n",
             block, MAX_ITERATIONS);
  }

  return total_side_effects;
}
