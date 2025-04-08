#include "cJSON.h"
#include "log.h"
#include "rdf.h"
#include "sqlite3.h"
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *load_file(const char *filename) {
  FILE *file = fopen(filename, "rb");
  if (!file)
    return NULL;

  fseek(file, 0, SEEK_END);
  long len = ftell(file);
  rewind(file);

  char *buffer = malloc(len + 1);
  if (!buffer) {
    fclose(file);
    return NULL;
  }

  fread(buffer, 1, len, file);
  buffer[len] = '\0';
  fclose(file);
  return buffer;
}

const char *anon_id(void) { return "0"; }

const char *get_value(cJSON *root, const char *key) {
  cJSON *val = cJSON_GetObjectItem(root, key);
  if (val && cJSON_IsString(val)) {
    return val->valuestring;
  }
  return NULL;
}

bool is_type(cJSON *root, const char *type) {
  cJSON *t = cJSON_GetObjectItem(root, "@type");
  if (!t)
    return false;

  if (cJSON_IsArray(t)) {
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, t) {
      if (cJSON_IsString(item) && strcmp(item->valuestring, type) == 0) {
        return true;
      }
    }
  } else if (cJSON_IsString(t)) {
    return strcmp(t->valuestring, type) == 0;
  }

  return false;
}

void insert_resolution_table_triples(sqlite3 *db, const char *block,
                                     const char *def_key,
                                     const char *resolution_json) {
  if (!resolution_json || strlen(resolution_json) == 0) {
    LOG_WARN("⚠️ Resolution JSON is empty for %s\n", def_key);
    return;
  }

  // Optionally store the raw string for debug/reference
  insert_triple(db, block, def_key, "inv:ResolutionTable", resolution_json);

  // Parse and store each entry as (def_key, input_pattern, output_value)
  cJSON *res_obj = cJSON_Parse(resolution_json);
  if (!res_obj || !cJSON_IsObject(res_obj)) {
    LOG_WARN("⚠️ Could not parse resolutionTable JSON for %s\n", def_key);
    return;
  }

  cJSON *row = NULL;
  cJSON_ArrayForEach(row, res_obj) {
    const char *key = row->string;
    const char *val = cJSON_GetStringValue(row);
    if (key && val) {
      insert_triple(db, block, def_key, key, val);
      LOG_INFO("🔢 ResolutionRow: %s[%s] = %s\n", def_key, key, val);
    }
  }

  cJSON_Delete(res_obj);
}

void process_definition(sqlite3 *db, const char *block, cJSON *root,
                        const char *raw_json, const char *fallback_name) {

  const char *name = get_value(root, "inv:name");
  if (!name)
    name = fallback_name;

  delete_expression_triples(db, block, name);

  LOG_INFO("\xF0\x9F\x93\x98 Definition: name=%s\n", name);
  insert_triple(db, block, name, "rdf:type", "inv:Definition");
  insert_triple(db, block, name, "inv:name", name);
  insert_triple(db, block, name, "context", raw_json);

  // --- SourceList ---
  cJSON *src_list = cJSON_GetObjectItem(root, "inv:SourceList");
  if (src_list && cJSON_IsArray(src_list)) {
    cJSON *src;
    cJSON_ArrayForEach(src, src_list) {
      const char *sname = get_value(src, "inv:name");
      if (!sname)
        continue;
      LOG_INFO("\xF0\x9F\x9F\xA2 [Expression] SourcePlace: name=%s\n", sname);
      insert_triple(db, block, sname, "rdf:type", "inv:SourcePlace");
      insert_triple(db, block, sname, "inv:name", sname);
    }
  }

  // --- DestinationList ---
  cJSON *dst_list = cJSON_GetObjectItem(root, "inv:DestinationList");
  if (dst_list && cJSON_IsArray(dst_list)) {
    cJSON *dst;
    cJSON_ArrayForEach(dst, dst_list) {
      const char *dname = get_value(dst, "inv:name");
      if (!dname)
        continue;
      LOG_INFO("\xF0\x9F\x94\xB5 [Expression] DestinationPlace: name=%s\n",
               dname);
      insert_triple(db, block, dname, "rdf:type", "inv:DestinationPlace");
      insert_triple(db, block, dname, "inv:name", dname);
    }
  }

  // --- Expand IO inputs ---
  cJSON *io = cJSON_GetObjectItem(root, "inv:IO");
  if (io && cJSON_IsObject(io)) {
    cJSON *inputs = cJSON_GetObjectItem(io, "inputs");
    if (inputs && cJSON_IsArray(inputs)) {
      cJSON *iname;
      cJSON_ArrayForEach(iname, inputs) {
        if (!cJSON_IsString(iname))
          continue;
        const char *sname = iname->valuestring;
        insert_triple(db, block, sname, "rdf:type", "inv:SourcePlace");
        insert_triple(db, block, sname, "inv:name", sname);
        insert_triple(db, block, name, "inv:SourceList", sname);
      }
    }

    cJSON *outputs = cJSON_GetObjectItem(io, "outputs");
    if (outputs && cJSON_IsArray(outputs)) {
      cJSON *oname;
      cJSON_ArrayForEach(oname, outputs) {
        if (!cJSON_IsString(oname))
          continue;
        const char *dname = oname->valuestring;
        insert_triple(db, block, dname, "rdf:type", "inv:DestinationPlace");
        insert_triple(db, block, dname, "inv:name", dname);
        insert_triple(db, block, name, "inv:DestinationList", dname);
      }
    }
  }

  // --- PlaceOfResolution ---
  cJSON *por = cJSON_GetObjectItem(root, "inv:PlaceOfResolution");
  if (por && cJSON_IsObject(por)) {
    LOG_INFO("\xF0\x9F\xA7\xA0 [Definition] PlaceOfResolution for %s\n", name);
    char por_key[128];
    snprintf(por_key, sizeof(por_key), "%s:por", name);
    insert_triple(db, block, por_key, "rdf:type", "inv:PlaceOfResolution");
    insert_triple(db, block, name, "inv:PlaceOfResolution", por_key);

    cJSON *frags = cJSON_GetObjectItem(por, "inv:hasExpressionFragment");
    if (frags && cJSON_IsArray(frags)) {
      for (int i = 0; i < cJSON_GetArraySize(frags); i++) {
        cJSON *frag = cJSON_GetArrayItem(frags, i);
        if (!frag)
          continue;

        char frag_id[128];
        snprintf(frag_id, sizeof(frag_id), "%s:frag%d", name, i);
        insert_triple(db, block, frag_id, "rdf:type", "inv:ExpressionFragment");
        insert_triple(db, block, por_key, "inv:hasExpressionFragment", frag_id);

        cJSON *cond = cJSON_GetObjectItem(frag, "inv:ConditionalInvocation");
        if (cond && cJSON_IsObject(cond)) {
          char cond_id[128];
          snprintf(cond_id, sizeof(cond_id), "%s:cond%d", name, i);
          insert_triple(db, block, cond_id, "rdf:type",
                        "inv:ConditionalInvocation");
          insert_triple(db, block, frag_id, "inv:ConditionalInvocation",
                        cond_id);

          cJSON *cond_inputs = cJSON_GetObjectItem(cond, "inv:Inputs");
          if (cond_inputs && cJSON_IsArray(cond_inputs)) {
            char *inputs_raw = cJSON_PrintUnformatted(cond_inputs);
            insert_triple(db, block, cond_id, "inv:Inputs", inputs_raw);
            free(inputs_raw);
          }

          const char *inv_name = get_value(cond, "inv:invocationName");
          if (inv_name) {
            LOG_INFO("\xF0\x9F\x93\x9B ConditionalInvocationName: %s\n",
                     inv_name);
            insert_triple(db, block, cond_id, "inv:invocationName", inv_name);
            cJSON *cond_inputs = cJSON_GetObjectItem(cond, "inv:Inputs");
            if (cond_inputs && cJSON_IsArray(cond_inputs)) {
              char *raw = cJSON_PrintUnformatted(cond_inputs);
              insert_triple(db, block, cond_id, "inv:Inputs", raw);
              free(raw);
            }
          }

          const char *output = get_value(cond, "inv:Output");
          if (output) {
            LOG_INFO("📤 ConditionalInvocation Output: %s\n", output);
            insert_triple(db, block, cond_id, "inv:Output", output);

            // 🩹 FIX: Make sure the destination place is registered
            insert_triple(db, block, output, "rdf:type",
                          "inv:DestinationPlace");
            insert_triple(db, block, output, "inv:name", output);
          } else {
            LOG_WARN("⚠️ inv:Output missing from ConditionalInvocation\n");
          }
        }
      }
    }
  }

  // --- Contained Definitions (shorthand resolution logic) ---
  cJSON *defs = cJSON_GetObjectItem(root, "inv:ContainsDefinition");
  if (defs && cJSON_IsArray(defs)) {
    for (int i = 0; i < cJSON_GetArraySize(defs); i++) {
      cJSON *def = cJSON_GetArrayItem(defs, i);
      if (!def)
        continue;

      // Simplified triple to store for now
      char def_key[128];
      snprintf(def_key, sizeof(def_key), "%s:def%d", name, i);

      insert_triple(db, block, def_key, "rdf:type", "inv:ContainedDefinition");
      insert_triple(db, block, name, "inv:ContainsDefinition", def_key);

      cJSON *table = cJSON_GetObjectItem(def, "inv:ResolutionTable");
      if (table && cJSON_IsObject(table)) {
        char *table_str = cJSON_PrintUnformatted(table);
        insert_resolution_table_triples(db, block, def_key, table_str);
        free(table_str);
      } else {
        LOG_WARN("⚠️ No JSON for Resolution table %s\n", def_key);
      }
    }
  }
}

void process_invocation(sqlite3 *db, const char *block, cJSON *root,
                        const char *json, const char *name) {
  LOG_INFO("📘 Invocation: name=%s\n", name);
  insert_triple(db, block, name, "rdf:type", "inv:Invocation");
  insert_triple(db, block, name, "inv:name", name);
  insert_triple(db, block, name, "context", json);
  cJSON *inputs = cJSON_GetObjectItem(root, "inv:Inputs");
  if (inputs && cJSON_IsArray(inputs)) {
    char *raw_inputs = cJSON_PrintUnformatted(inputs);
    insert_triple(db, block, name, "inv:Inputs", raw_inputs);
    free(raw_inputs);
  }

  // --- SourceList ---
  cJSON *src_list = cJSON_GetObjectItem(root, "inv:SourceList");
  if (src_list && cJSON_IsArray(src_list)) {
    cJSON *src;
    cJSON_ArrayForEach(src, src_list) {
      const char *sname = get_value(src, "inv:name");
      const char *val = get_value(src, "inv:hasContent");
      if (!sname)
        continue;

      insert_triple(db, block, sname, "rdf:type", "inv:SourcePlace");
      insert_triple(db, block, sname, "inv:name", sname);
      if (val)
        insert_triple(db, block, sname, "inv:hasContent", val);
      insert_triple(db, block, sname, "inv:SourceList", sname);

      LOG_INFO("🟢 Invocation SourcePlace: %s = %s\n", sname,
               val ? val : "(null)");
    }
  }

  // --- DestinationList ---
  cJSON *dst_list = cJSON_GetObjectItem(root, "inv:DestinationList");
  if (dst_list && cJSON_IsArray(dst_list)) {
    cJSON *dst;
    cJSON_ArrayForEach(dst, dst_list) {
      const char *dname = get_value(dst, "inv:name");
      if (!dname)
        continue;

      insert_triple(db, block, dname, "rdf:type", "inv:DestinationPlace");
      insert_triple(db, block, dname, "inv:name", dname);
      insert_triple(db, block, name, "inv:DestinationList", dname);

      LOG_INFO("🔵 Invocation DestinationPlace: %s\n", dname);
    }
  }
}

void map_io(sqlite3 *db, const char *block, const char *inv_dir) {
  DIR *dir = opendir(inv_dir);
  struct dirent *entry;
  bool matched = false;
  LOG_INFO("\xF0\x9F\x94\x8D map_io starting. in %s\n", inv_dir);

  if (!dir) {
    LOG_ERROR("\xE2\x9D\x8C '%s' directory not found. Skipping map_io.",
              inv_dir);
    return;
  }

  while ((entry = readdir(dir)) != NULL) {
    if (strstr(entry->d_name, ".json")) {
      char path[256];
      snprintf(path, sizeof(path), "%s/%s", inv_dir, entry->d_name);
      LOG_INFO("Loading %s\n", path);
      char *json = load_file(path);
      if (!json)
        continue;

      cJSON *root = cJSON_Parse(json);
      if (!root) {
        free(json);
        continue;
      }

      const char *name = get_value(root, "inv:name");
      if (!name)
        name = entry->d_name; // fallback ID from file

      if (is_type(root, "inv:Definition")) {
        matched = true;
        process_definition(db, block, root, json, name);
      }

      if (is_type(root, "inv:Invocation")) {
        matched = true;
        process_invocation(db, block, root, json, name);
      }

      if (!matched) {
        const char *type = get_value(root, "@type");
        if (type) {
          LOG_WARN("⚠️ Unrecognized type: %s in file %s\n", type, entry->d_name);
        } else {
          LOG_WARN("⚠️ No @type found in file %s\n", entry->d_name);
        }
      }
      cJSON_Delete(root);
      free(json);
    }
  }
  closedir(dir);
}
