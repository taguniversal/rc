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
char *build_resolution_key(sqlite3 *db, const char *invocation_str) {
  if (!invocation_str)
    return NULL;

  char *inv_str_copy = strdup(invocation_str);
  const char *p = inv_str_copy;

  LOG_INFO("BEGIN build_resolution_key invocation_str: %s (%p)\n", inv_str_copy,
           (void *)invocation_str);

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

      const char *var_id = lookup_subject_by_name(db, var);
      const char *val = lookup_object(db, var_id, "inv:hasContent");

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
  return strdup(key);
}

int resolve_conditional_invocation(sqlite3 *db, const char *block,
                                   const char *expr_id) {
  LOG_INFO("🧠 Attempting resolution from ConditionalInvocation in %s\n",
           expr_id);

  const char *por = lookup_object(db, expr_id, "inv:PlaceOfResolution");
  if (!por)
    return 0;

  const char *frag = lookup_object(db, por, "inv:hasExpressionFragment");
  if (!frag)
    return 0;

  const char *cond = lookup_object(db, frag, "inv:ConditionalInvocation");
  if (!cond)
    return 0;

  const char *template = lookup_object(db, cond, "inv:invocationName");
  if (!template) {
    LOG_WARN("⚠️ Missing invocationName template in ConditionalInvocation\n");
    return 0;
  }

  LOG_INFO("🧪 Raw template: '%s' (len=%zu)", template, strlen(template));
  LOG_INFO("Calling build_resolution_key with: %s (%p)\n", template,
           (void *)template);
  char *key = build_resolution_key(db, template);
  if (!key) {
    LOG_WARN("⚠️ Could not build resolution key from template: %s\n", template);
    return 0;
  }

  LOG_INFO("🧩 Built resolution key from template: %s → %s\n", template, key);

  const char *output_name = lookup_object(db, cond, "inv:Output");
  if (!output_name) {
    LOG_WARN("⚠️ No Output defined in ConditionalInvocation\n");
    free(key);
    return 0;
  }

  const char *output_id = lookup_subject_by_name(db, output_name);
  if (!output_id) {
    LOG_WARN("⚠️ Could not resolve output name to ID: %s\n", output_name);
    free(key);
    return 0;
  }

  const char *def_id = lookup_object(db, expr_id, "inv:ContainsDefinition");
  if (!def_id) {
    LOG_WARN("⚠️ Missing ContainsDefinition in %s\n", expr_id);
    free(key);
    return 0;
  }

  const char *result = lookup_object(db, def_id, key);
  if (!result) {
    LOG_WARN("❌ Resolution table has no entry for key: %s\n", key);
    free(key);
    return 0;
  }

  LOG_INFO("📥 Inserting resolution result %s into %s\n", result, output_id);
  insert_triple(db, block, output_id, "inv:hasContent", result);
  free(key);
  return 1;
}

int resolve_definition_output(sqlite3 *db, const char *block,
                              const char *definition_id) {
  LOG_INFO("resolve_definition_output for %s\n", definition_id);

  const char *type = lookup_object(db, definition_id, "rdf:type");
  LOG_INFO("📎 Found rdf:type = [%s] for definition [%s]\n",
           type ? type : "(null)", definition_id ? definition_id : "(null)");

  if (!type || (strcmp(type, "inv:Definition") != 0 &&
                strcmp(type, "inv:ContainedDefinition") != 0)) {
    LOG_WARN("No valid type found for %s — got %s\n", definition_id,
             type ? type : "(null)");
    return 0;
  }

  const char *por = lookup_object(db, definition_id, "inv:PlaceOfResolution");
  if (!por) {
    LOG_WARN("No Place of Resolution found\n");
    return 0;
  }

  const char *frag = lookup_object(db, por, "inv:hasExpressionFragment");
  if (!frag) {
    LOG_WARN("No Expression fragment found\n");
    return 0;
  }

  const char *cond = lookup_object(db, frag, "inv:ConditionalInvocation");
  if (!cond) {
    LOG_WARN("⚠️ Missing conditional invocation for frag %s\n", frag);
    return 0;
  }

  LOG_INFO("📎 Fetched ConditionalInvocation: %s\n", cond);

  const char *invocation_str =
      lookup_object(db, cond, "inv:ResolvedInvocationName");
  if (!invocation_str) {
    invocation_str = lookup_object(db, cond, "inv:invocationName");
    LOG_INFO("🧩 Fallback to template name: %s\n",
             invocation_str ? invocation_str : "(null)");
  }

  const char *output_name = lookup_object(db, cond, "inv:Output");
  if (!invocation_str || !output_name) {
    LOG_WARN("⚠️ Missing required fields in ConditionalInvocation %s\n", cond);
    return 0;
  }

  LOG_INFO("🔧 cond = %s, output_name = %s\n", cond, output_name);

  const char *output_id = lookup_subject_by_name(db, output_name);
  if (!output_id) {
    LOG_WARN("⚠️ Could not resolve subject for output name: %s\n", output_name);
    return 0;
  }

  char *key = build_resolution_key(db, invocation_str);
  if (!key) {
    LOG_WARN("⚠️ Failed to build resolution key from: %s\n", invocation_str);
    return 0;
  }

  LOG_INFO("🧪 Resolution lookup key: %s\n", key);

  const char *def_node =
      lookup_object(db, definition_id, "inv:ContainsDefinition");
  const char *result = lookup_object(db, def_node, key);

  if (!result) {
    LOG_WARN("⚠️ Resolution table missing entry for key: %s\n", key);
    free(key);
    return 0;
  }

  LOG_INFO("📥 Writing resolution result %s into %s\n", result, output_id);
  insert_triple(db, block, output_id, "inv:hasContent", result);
  free(key);
  return 1;
}

void eval_resolution_table(sqlite3 *db, const char *block,
                           const char *expr_id) {
  LOG_INFO("🔍 Resolution table eval for: %s at block %s\n", expr_id, block);

  const char *por = lookup_object(db, expr_id, "inv:PlaceOfResolution");
  if (!por)
    return;

  const char *frag = lookup_object(db, por, "inv:hasExpressionFragment");
  if (!frag)
    return;

  const char *cond_inv = lookup_object(db, frag, "inv:ConditionalInvocation");
  if (!cond_inv) {
    LOG_WARN("❌ No ConditionalInvocation found for resolution.\n");
    return;
  }

  const char *destination_name = lookup_object(db, cond_inv, "inv:Output");
  if (!destination_name) {
    LOG_WARN("❌ No Output defined in ConditionalInvocation.\n");
    return;
  }

  const char *destination_place = lookup_subject_by_name(db, destination_name);
  if (!destination_place || *destination_place == '\0') {
    LOG_ERROR("❗ destination_place is NULL or empty — aborting insert!\n");
    return;
  }

  const char *invocation_name =
      lookup_object(db, cond_inv, "inv:invocationName");
  if (!invocation_name) {
    LOG_WARN("⚠️ Missing invocationName on ConditionalInvocation\n");
    return;
  }

  // Construct the key by replacing $X with X's content
  char *key;
  key = build_resolution_key(db, invocation_name);
  const char *p = invocation_name;
  while (*p) {
    if (*p == '$') {
      p++;
      char var[32] = {0};
      int vi = 0;
      while (*p && isalnum(*p))
        var[vi++] = *p++;
      var[vi] = '\0';

      const char *input_id = lookup_subject_by_name(db, var);
      const char *val = lookup_object(db, input_id, "inv:hasContent");
      key[strlen(key)] = val ? val[0] : '_';
    } else {
      p++;
    }
  }

  LOG_INFO("🧪 Lookup resolution for key %s in expr %s\n", key, expr_id);

  const char *def_node = lookup_object(db, expr_id, "inv:ContainsDefinition");
  const char *result = lookup_object(db, def_node, key);
  if (!result) {
    LOG_WARN("❌ Resolution table missing entry for key %s\n", key);
    free(key);
    return;
  }

  LOG_INFO("📥 Inserting result into: %s hasValue %s\n", destination_place,
           result);
  insert_triple(db, block, destination_place, "inv:hasValue", result);

  const char *check = lookup_object(db, destination_place, "inv:hasValue");
  LOG_INFO("🔎 Confirmed in DB: %s hasValue %s\n", destination_place,
           check ? check : "(null)");
  free(key);
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

  char *values_json = lookup_raw_value(db, expr_id, "inv:Inputs");
  if (!values_json) {
    LOG_WARN("⚠️ No input values found in invocation\n");
    return 0;
  }

  // Only Definitions get bound to Invocation input values
  const char *type = lookup_object(db, expr_id, "rdf:type");
  if (!type || strcmp(type, "inv:Definition") != 0)
    return 0;

  const char *def_name = lookup_object(db, expr_id, "inv:name");
  if (!def_name)
    return 0;

  const char *inv_id = find_invocation_by_name(db, def_name);
  if (!inv_id) {
    LOG_WARN("⚠️ No matching Invocation found for Definition %s\n", def_name);
    return 0;
  }

  cJSON *input_vals = cJSON_Parse(values_json);
  if (!input_vals || !cJSON_IsArray(input_vals)) {
    LOG_ERROR("❌ Failed to parse inv:Inputs array from invocation\n");
    free(values_json);
    return 0;
  }

  // Now iterate the SourceList from the Definition
  sqlite3_stmt *stmt;
  const char *sql = "SELECT object FROM triples WHERE subject = ? AND "
                    "predicate = 'inv:SourceList'";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ Failed to prepare SourceList lookup: %s\n",
              sqlite3_errmsg(db));
    cJSON_Delete(input_vals);
    free(values_json);
    return 0;
  }

  sqlite3_bind_text(stmt, 1, expr_id, -1, SQLITE_STATIC);

  int index = 0;
  int side_effect = 0;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *source_id = (const char *)sqlite3_column_text(stmt, 0);
    const char *name = lookup_object(db, source_id, "inv:name");

    cJSON *val_item = cJSON_GetArrayItem(input_vals, index++);
    if (!name || !val_item || !cJSON_IsString(val_item))
      continue;

    const char *value = val_item->valuestring;
    LOG_INFO("📥 Binding %s = %s into %s\n", name, value, source_id);
    insert_triple(db, block, source_id, "inv:hasContent", value);
    side_effect = 1;
  }

  sqlite3_finalize(stmt);
  cJSON_Delete(input_vals);
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
  const char *por = lookup_object(db, expr_id, "inv:PlaceOfResolution");
  if (por) {
    const char *frag = lookup_object(db, por, "inv:hasExpressionFragment");
    if (frag) {
      const char *cond = lookup_object(db, frag, "inv:ConditionalInvocation");
      LOG_INFO("📍 Fetched ConditionalInvocation: %s\n", cond);
      if (cond) {
        const char *template = lookup_object(db, cond, "inv:invocationName");
        if (template && strchr(template, '$')) {
          LOG_INFO("🔧 template from %s is: %s\n", cond, template);
          char *resolved = build_resolution_key(db, template);
          if (resolved) {
            LOG_INFO("🧩 Resolved dynamic invocationName = %s\n", resolved);
            insert_triple(db, block, cond, "inv:ResolvedInvocationName",
                          resolved);
            free(resolved);
            side_effect = 1;
          } else {
            LOG_WARN("⚠️ Failed to build invocationName for cond = %s\n", cond);
          }
        }
      }
    }
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
    const char *dest_name = lookup_object(db, dest_id, "inv:name");

    if (!dest_name) {
      LOG_WARN("⚠️ DestinationPlace %s has no name\n", dest_id);
      continue;
    }

    sqlite3_stmt *src_stmt;
    const char *src_sql = "SELECT subject FROM triples WHERE psi = ? AND "
                          "predicate = 'inv:name' AND object = ?";
    if (sqlite3_prepare_v2(db, src_sql, -1, &src_stmt, NULL) != SQLITE_OK) {
      LOG_ERROR("Failed to prepare source match query.");
      continue;
    }

    sqlite3_bind_text(src_stmt, 1, block, -1, SQLITE_STATIC);
    sqlite3_bind_text(src_stmt, 2, dest_name, -1, SQLITE_STATIC);

    if (sqlite3_step(src_stmt) == SQLITE_ROW) {
      const char *source_id = (const char *)sqlite3_column_text(src_stmt, 0);
      const char *content = lookup_object(db, source_id, "inv:hasContent");

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
      LOG_WARN(
          "⚠️ No matching SourcePlace found for DestinationPlace name: %s\n",
          dest_name);
    }

    sqlite3_finalize(src_stmt);
  }

  sqlite3_finalize(stmt);

  int resolved_conditional_invocation = resolve_conditional_invocation(db, block, expr_id);
  side_effect |= resolved_conditional_invocation;
  // Step 3: Resolution table output
  const char *def_id = lookup_object(db, expr_id, "inv:ContainsDefinition");
  // Step 3: Resolution table output — only if ConditionalInvocation didn't run
  if (!resolved_conditional_invocation && def_id) {
    LOG_INFO("📎 Passing def_id into resolve_definition_output: %s\n", def_id);
    side_effect |= resolve_definition_output(db, block, def_id);
  }

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
