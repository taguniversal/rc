#include "log.h"
#include "rdf.h"
#include "sqlite3.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>  // fprintf, stderr
#include <stdlib.h> // general utilities (optional but common)
#include <string.h>
#include <time.h>
#include <time.h>   // time_t, localtime, strftime
#include <unistd.h> // isatty, fileno

#define MAX_ITERATIONS 2 // Or whatever feels safe for your app

char *build_resolution_key(sqlite3 *db, const char* block, const char *invocation_str) {
  if (!invocation_str)
    return NULL;

  char *inv_str_copy = strdup(invocation_str);
  const char *p = inv_str_copy;

  LOG_INFO("BEGIN build_resolution_key invocation_str: %s (%p)\n", inv_str_copy,
           (void *)invocation_str);

  char key[256] = {0};
  char *var_id;
  char *val;

  while (*p) {
    if (*p == '$') {
      p++; // skip '$'
      char var[64] = {0};
      int vi = 0;
      while (*p && isalnum(*p)) {
        var[vi++] = *p++;
      }
      var[vi] = '\0';

      var_id = lookup_subject_by_name(db, block, var);
      val = lookup_object(db, block, var_id, "inv:hasContent");

      LOG_INFO("🔍 Building key — resolving var: %s", var);
      LOG_INFO("     var_id = %s", var_id ? var_id : "(null)");
      LOG_INFO("     val    = %s", val ? val : "(null)");

      size_t len = strlen(key);
      key[len] = val ? val[0] : '_';
      key[len + 1] = '\0';
    } else {
      // Skip characters like '(', ')', etc.
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
  free(val);
  free(var_id);
  return strdup(key);
}

int resolve_conditional_invocation(sqlite3 *db, const char *block,
                                   const char *expr_id) {
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
  char *key = build_resolution_key(db, block, template);
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

  char *def_id = lookup_object(db, block, expr_id, "inv:ContainsDefinition");
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

  char *result = lookup_object(db, block, def_id, key);
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

  LOG_INFO("📥 Inserting resolution result %s into %s\n", result, output_id);
  insert_triple(db, block, output_id, "inv:hasContent", result);
  free(result);
  free(output_id);
  free(por);
  free(frag);
  free(cond);
  free(template);
  free(key);
  return 1;
}

int resolve_definition_output(sqlite3 *db, const char *block,
                              const char *definition_id) {
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

  char *cond = lookup_object(db, block, frag, "inv:ConditionalInvocation");
  if (!cond) {
    LOG_WARN("⚠️ Missing conditional invocation for frag %s\n", frag);
    free(type);
    free(por);
    free(frag);
    return 0;
  }

  LOG_INFO("📎 Fetched ConditionalInvocation: %s\n", cond);

  char *invocation_str = lookup_object(db, block, cond, "inv:ResolvedInvocationName");
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

  char *key = build_resolution_key(db, block, invocation_str);
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

  char *def_node = lookup_object(db, block, definition_id, "inv:ContainsDefinition");
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

  LOG_INFO("📥 Writing resolution result %s into %s\n", result, output_id);
  insert_triple(db, block, output_id, "inv:hasContent", result);

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
  return 1;
}
void eval_resolution_table(sqlite3 *db, const char *block,
                           const char *expr_id) {
  LOG_INFO("🔍 Resolution table eval for: %s at block %s\n", expr_id, block);

  char *por = lookup_object(db, block, expr_id, "inv:PlaceOfResolution");
  if (!por)
    return;

  char *frag = lookup_object(db, block, por, "inv:hasExpressionFragment");
  if (!frag) {
    free(por);
    return;
  }

  char *cond_inv = lookup_object(db, block, frag, "inv:ConditionalInvocation");
  if (!cond_inv) {
    LOG_WARN("❌ No ConditionalInvocation found for resolution.\n");
    free(por);
    free(frag);
    return;
  }

  char *destination_name = lookup_object(db, block, cond_inv, "inv:Output");
  if (!destination_name) {
    LOG_WARN("❌ No Output defined in ConditionalInvocation.\n");
    free(por);
    free(frag);
    free(cond_inv);
    return;
  }

  char *destination_place = lookup_subject_by_name(db, block, destination_name);
  if (!destination_place || *destination_place == '\0') {
    LOG_ERROR("❗ destination_place is NULL or empty — aborting insert!\n");
    free(por);
    free(frag);
    free(cond_inv);
    free(destination_name);
    free(destination_place);
    return;
  }

  char *invocation_name = lookup_object(db, block, cond_inv, "inv:invocationName");
  if (!invocation_name) {
    LOG_WARN("⚠️ Missing invocationName on ConditionalInvocation\n");
    free(por);
    free(frag);
    free(cond_inv);
    free(destination_name);
    free(destination_place);
    return;
  }

  char *key = build_resolution_key(db, block, invocation_name);
  if (!key) {
    LOG_WARN("⚠️ Failed to build resolution key\n");
    free(por);
    free(frag);
    free(cond_inv);
    free(destination_name);
    free(destination_place);
    free(invocation_name);
    return;
  }

  LOG_INFO("🧪 Lookup resolution for key %s in expr %s\n", key, expr_id);

  char *def_node = lookup_object(db, block, expr_id, "inv:ContainsDefinition");
  char *result = lookup_object(db, block, def_node, key);

  if (!result) {
    LOG_WARN("❌ Resolution table missing entry for key %s\n", key);
    free(por);
    free(frag);
    free(cond_inv);
    free(destination_name);
    free(destination_place);
    free(invocation_name);
    free(key);
    free(def_node);
    return;
  }

  LOG_INFO("📥 Inserting result into: %s hasValue %s\n", destination_place,
           result);
  insert_triple(db, block, destination_place, "inv:hasValue", result);

  char *check = lookup_object(db, block, destination_place, "inv:hasValue");
  LOG_INFO("🔎 Confirmed in DB: %s hasValue %s\n", destination_place,
           check ? check : "(null)");

  // Clean up everything
  free(por);
  free(frag);
  free(cond_inv);
  free(destination_name);
  free(destination_place);
  free(invocation_name);
  free(key);
  free(def_node);
  free(result);
  free(check);
}

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
int bind_inputs(sqlite3 *db, const char *block, const char *expr_id) {
  LOG_INFO("🔗 Binding inputs for expression: %s\n", expr_id);

  char *values_json = lookup_raw_value(db, block, expr_id, "inv:Inputs");
  if (!values_json) {
    LOG_WARN("⚠️ No input values found in invocation\n");
    return 0;
  }

  char *type = lookup_object(db, block, expr_id, "rdf:type");
  if (!type || strcmp(type, "inv:Definition") != 0) {
    free(type);
    free(values_json);
    return 0;
  }

  char *def_name = lookup_object(db, block, expr_id, "inv:name");
  if (!def_name) {
    free(type);
    free(values_json);
    return 0;
  }

  char *inv_id = find_invocation_by_name(db, block, def_name);
  if (!inv_id) {
    LOG_WARN("⚠️ No matching Invocation found for Definition %s\n", def_name);
    free(type);
    free(def_name);
    free(values_json);
    return 0;
  }

  cJSON *input_vals = cJSON_Parse(values_json);
  if (!input_vals || !cJSON_IsArray(input_vals)) {
    LOG_ERROR("❌ Failed to parse inv:Inputs array from invocation\n");
    free(type);
    free(def_name);
    free(inv_id);
    free(values_json);
    return 0;
  }

  sqlite3_stmt *stmt;
  const char *sql = "SELECT object FROM triples WHERE subject = ? AND "
                    "predicate = 'inv:SourceList'";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ Failed to prepare SourceList lookup: %s\n",
              sqlite3_errmsg(db));
    cJSON_Delete(input_vals);
    free(type);
    free(def_name);
    free(inv_id);
    free(values_json);
    return 0;
  }

  sqlite3_bind_text(stmt, 1, expr_id, -1, SQLITE_STATIC);

  int index = 0;
  int side_effect = 0;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *source_id = (const char *)sqlite3_column_text(stmt, 0);
    char *name = lookup_object(db, block, source_id, "inv:name");

    cJSON *val_item = cJSON_GetArrayItem(input_vals, index++);
    if (!name || !val_item || !cJSON_IsString(val_item)) {
      free(name);
      continue;
    }

    const char *value = val_item->valuestring;
    LOG_INFO("📥 Binding %s = %s into %s\n", name, value, source_id);
    insert_triple(db, block, source_id, "inv:hasContent", value);
    side_effect = 1;
    free(name);
  }

  sqlite3_finalize(stmt);
  cJSON_Delete(input_vals);
  free(type);
  free(def_name);
  free(inv_id);
  free(values_json);
  return side_effect;
}
int cycle(sqlite3 *db, const char *block, const char *expr_id) {
  LOG_INFO("🔁 Cycle pass for expression: %s\n", expr_id);
  int side_effect = 0;

  // Step 1: Bind inputs
  if (bind_inputs(db, block, expr_id)) {
    LOG_INFO("🔗 Input binding caused side effects.\n");
    side_effect = 1;
  } else {
    LOG_INFO("🔗 Input binding caused NO side effects.\n");
  }

  // Step 1.5: Evaluate ConditionalInvocation dynamic names
  char *por = lookup_object(db, block, expr_id, "inv:PlaceOfResolution");
  if (por) {
    char *frag = lookup_object(db, block, por, "inv:hasExpressionFragment");
    if (frag) {
      char *cond = lookup_object(db, block, frag, "inv:ConditionalInvocation");
      LOG_INFO("📍 Fetched ConditionalInvocation: %s\n", cond);
      if (cond) {
        char *template = lookup_object(db, block, cond, "inv:invocationName");
        if (template && strchr(template, '$')) {
          LOG_INFO("🔧 template from %s is: %s\n", cond, template);
          char *resolved = build_resolution_key(db, block, template);
          if (resolved) {
            LOG_INFO("🧩 Resolved dynamic invocationName = %s\n", resolved);
            insert_triple(db, block, cond, "inv:ResolvedInvocationName",
                          resolved);
            free(resolved);
            side_effect = 1;
          } else {
            LOG_WARN("⚠️ Failed to build invocationName for cond = %s\n", cond);
          }
          free(template);
        } else {
          free(template);
        }
        free(cond);
      }
      free(frag);
    }
    free(por);
  }

  // Step 2: Propagate content along destination_list edges
  sqlite3_stmt *stmt;
  const char *sql = "SELECT object FROM triples WHERE psi = ? AND subject = ? "
                    "AND predicate = 'inv:destination_list'";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("Failed to prepare destination_list query.");
    return side_effect;
  }

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, expr_id, -1, SQLITE_STATIC);

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
        LOG_INFO("📤 %s → %s : %s\n", source_id, dest_id, content);
        insert_triple(db, block, dest_id, "inv:hasContent", content);
        side_effect = 1;
      } else {
        LOG_INFO("🔁 Clearing %s (no content in source)\n", dest_id);
        delete_triple(db, block, dest_id, "inv:hasContent");
        side_effect = 1;
      }

      free(content);
    } else {
      LOG_WARN(
          "⚠️ No matching SourcePlace found for DestinationPlace name: %s\n",
          dest_name);
    }

    sqlite3_finalize(src_stmt);
    free(dest_name);
  }

  sqlite3_finalize(stmt);

  // Step 2.5: Conditional resolution
  int resolved_conditional_invocation =
      resolve_conditional_invocation(db, block, expr_id);
  side_effect |= resolved_conditional_invocation;

  // Step 3: Resolution table output — only if ConditionalInvocation didn't run
  char *def_id = lookup_object(db, block, expr_id, "inv:ContainsDefinition");
  if (!resolved_conditional_invocation && def_id) {
    LOG_INFO("📎 Passing def_id into resolve_definition_output: %s\n", def_id);
    side_effect |= resolve_definition_output(db, block, def_id);
  }
  free(def_id);

  return side_effect;
}

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
      const char *expr_id = (const char *)sqlite3_column_text(stmt, 0);
      LOG_INFO("📘 Evaluating expression: %s\n", expr_id);

      if (cycle(db, block, expr_id)) {
        side_effect_this_round = 1;
        total_side_effects = 1;
      }
    }

    sqlite3_finalize(stmt);

    if (!side_effect_this_round) {
      LOG_INFO("✅ Eval for %s stabilized after %d iteration(s)\n", block,
               iteration);
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
