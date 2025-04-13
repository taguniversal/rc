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

      LOG_INFO("🔍 Building key — resolving var: %s", var);
      LOG_INFO("     var_id = %s", scoped_var_id ? scoped_var_id : "(null)");
      LOG_INFO("     val    = %s", val ? val : "(null)");

      size_t len = strlen(key);
      key[len] = val ? val[0] : '_';
      key[len + 1] = '\0';

      free(scoped_var_id);
      free(val);
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
                                   const char *expr_id) {
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
  const char *sql = "SELECT object FROM triples WHERE psi = ? AND subject = ? "
                    "AND predicate = 'inv:SourceList'";
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

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, expr_id, -1, SQLITE_STATIC);

  int index = 0;
  int side_effect = 0;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *scoped_source_id = (const char *)sqlite3_column_text(stmt, 0);

    cJSON *val_item = cJSON_GetArrayItem(input_vals, index++);
    if (!val_item || !cJSON_IsString(val_item)) {
      continue;
    }

    const char *value = val_item->valuestring;

    char *existing_value =
        lookup_object(db, block, scoped_source_id, "inv:hasContent");

    if (!existing_value || strcmp(existing_value, value) != 0) {
      LOG_INFO("📅 Binding [%s] = %s\n", scoped_source_id, value);
      insert_triple(db, block, scoped_source_id, "inv:hasContent", value);
      side_effect = 1;
    } else {
      LOG_INFO("⚖️  Skipping bind: [%s] already has content = %s\n",
               scoped_source_id, existing_value);
    }

    free(existing_value);
  }

  sqlite3_finalize(stmt);
  cJSON_Delete(input_vals);
  free(type);
  free(def_name);
  free(inv_id);
  free(values_json);
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
                  build_resolution_key(db, block, expr_id, template);
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

    LOG_INFO("🪵 DEBUG: Found DestinationPlace name = %s (%s)\n", dest_name,
             dest_id); // 🪵 DEBUG

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
          // No content in source, check if destination currently has content to
          // clear
          char *existing = lookup_object(db, block, dest_id, "inv:hasContent");

          if (existing) {
            LOG_INFO("🔁 Clearing %s (no content in source)\n", dest_id);
            delete_triple(db, block, dest_id, "inv:hasContent");
            side_effect = 1;
          } else {
            LOG_INFO("⚖️  Skipping clear: %s has no content anyway\n", dest_id);
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

  // Step 2.5: Conditional resolution
  int resolved_conditional_invocation =
      resolve_conditional_invocation(db, block, expr_id);
  LOG_INFO("🪵 DEBUG: resolve_conditional_invocation returned %d\n",
           resolved_conditional_invocation); // 🪵 DEBUG
  side_effect |= resolved_conditional_invocation;

  // Step 3: Resolution table output — only if ConditionalInvocation didn't run
  char *def_id = lookup_object(db, block, expr_id, "inv:ContainsDefinition");
  if (!resolved_conditional_invocation && def_id) {
    if (!is_contained_definition(db, block, def_id)) {
      LOG_INFO("📎 Passing def_id into resolve_definition_output: %s\n",
               def_id);
      int output_side_effect = resolve_definition_output(db, block, def_id);
      LOG_INFO("🪵 DEBUG: resolve_definition_output returned %d\n",
               output_side_effect); // 🪵 DEBUG
      side_effect |= output_side_effect;
    } else {
      LOG_INFO(
          "🔬 Skipping resolve_definition_output for ContainedDefinition: %s\n",
          def_id);
    }
  }
  free(def_id);

  LOG_INFO("🔚 cycle(%s, %s) returned side_effect = %d\n", block, expr_id,
           side_effect); // 🪵 DEBUG

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
        total_side_effects = total_side_effects + 1;
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
