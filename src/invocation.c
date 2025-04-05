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

      if (is_type(root, "inv:Invocation")) {
        matched = true;
        LOG_INFO("\xF0\x9F\x93\x98 Invocation: name=%s\n", name);
        insert_triple(db, block, name, "rdf:type", "inv:Invocation");
        insert_triple(db, block, name, "inv:name", name);
        insert_triple(db, block, name, "context", json);

        cJSON *src_list = cJSON_GetObjectItem(root, "inv:source_list");
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
            insert_triple(db, block, sname, "inv:source_list", sname);
            LOG_INFO("\xF0\x9F\x9F\xA2 Invocation SourcePlace: %s = %s\n",
                     sname, val ? val : "(null)");
          }
        }

        cJSON *dst_list = cJSON_GetObjectItem(root, "inv:destination_list");
        if (dst_list && cJSON_IsArray(dst_list)) {
          cJSON *dst;
          cJSON_ArrayForEach(dst, dst_list) {
            const char *dname = get_value(dst, "inv:name");
            if (!dname)
              continue;
            insert_triple(db, block, dname, "rdf:type", "inv:DestinationPlace");
            insert_triple(db, block, dname, "inv:name", dname);
            insert_triple(db, block, name, "inv:destination_list", dname);
            LOG_INFO("\xF0\x9F\x94\xB5 Invocation DestinationPlace: %s\n",
                     dname);
          }
        }
      }

      if (is_type(root, "inv:Definition")) {
        matched = true;
        const char *name = get_value(root, "inv:name");
        if (!name) {
          LOG_WARN("❌ Expression missing 'inv:name'\n");
          cJSON_Delete(root);
          free(json);
          continue;
        }

        delete_expression_triples(db, block, name);
        LOG_INFO("📘 Definition: name=%s\n", name);
        insert_triple(db, block, name, "rdf:type", "inv:Definition");
        insert_triple(db, block, name, "inv:name", name);
        insert_triple(db, block, name, "context", json);

        // --- Source_list ---
        cJSON *src_list = cJSON_GetObjectItem(root, "inv:source_list");
        if (src_list && cJSON_IsArray(src_list)) {
          for (int i = 0; i < cJSON_GetArraySize(src_list); i++) {
            cJSON *src = cJSON_GetArrayItem(src_list, i);
            const char *sname = get_value(src, "inv:name");
            if (!sname) {
              LOG_WARN("No name four for source %i of %s\n", i, sname);
              continue;
            }

            LOG_INFO("🟢 [Expression] SourcePlace: name=%s\n", name);
            insert_triple(db, block, sname, "rdf:type", "inv:SourcePlace");
            insert_triple(db, block, sname, "inv:name", name);
          }
        }

        // --- NEW: destination_list ---
        cJSON *dst_list = cJSON_GetObjectItem(root, "inv:destination_list");
        if (dst_list && cJSON_IsArray(dst_list)) {
          for (int i = 0; i < cJSON_GetArraySize(dst_list); i++) {
            cJSON *dst = cJSON_GetArrayItem(dst_list, i);
            const char *name = get_value(dst, "inv:name");

            if (name)
              insert_triple(db, block, name, "rdf:type",
                            "inv:DestinationPlace");
          }
        }

        // --- NEW: embedded PlaceOfResolution ---
        cJSON *por = cJSON_GetObjectItem(root, "inv:place_of_resolution");
        if (por && cJSON_IsObject(por)) {
          LOG_INFO("🧠 [Definition] PlaceOfResolution for %s\n",
                   name ? name : "(unnamed)");
          char por_key[64];
          snprintf(por_key, sizeof(por_key), "%s:por", name);
          insert_triple(db, block, name, "inv:place_of_resolution", por_key);
          insert_triple(db, block, por_key, "rdf:type",
                        "inv:PlaceOfResolution");

          cJSON *fragments =
              cJSON_GetObjectItem(por, "inv:hasExpressionFragment");
          if (fragments && cJSON_IsArray(fragments)) {
            for (int i = 0; i < cJSON_GetArraySize(fragments); i++) {
              cJSON *fragment = cJSON_GetArrayItem(fragments, i);
              if (!fragment || !cJSON_IsObject(fragment))
                continue;

              LOG_INFO("🔩 ExpressionFragment %d in PlaceOfResolution\n", i);
              char frag_name[32];
              snprintf(frag_name, sizeof(frag_name), "%s:frag%d", name, i);
              insert_triple(db, block, frag_name, "rdf:type",
                            "inv:ExpressionFragment");
              insert_triple(db, block, por_key, "inv:hasExpressionFragment",
                            frag_name);

              const char *destinationPlace =
                  get_value(fragment, "inv:destinationPlace");
              const char *def = get_value(fragment, "inv:invokesDefinition");
              const char *args = get_value(fragment, "inv:invocationArguments");

              if (destinationPlace)
                insert_triple(db, block, frag_name, "inv:destinationPlace",
                              destinationPlace);
              if (def)
                insert_triple(db, block, frag_name, "inv:invokesDefinition",
                              def);
              if (args)
                insert_triple(db, block, frag_name, "inv:invocationArguments",
                              args);

              // Handle nested ConditionalInvocation
              cJSON *cond_inv =
                  cJSON_GetObjectItem(fragment, "inv:hasConditionalInvocation");
              if (cond_inv && cJSON_IsObject(cond_inv)) {
                LOG_INFO("🧠 ConditionalInvocation for fragment %d\n", i);
                char cond_name[32];
                snprintf(cond_name, sizeof(cond_name), "%s:cond%d", name, i);
                insert_triple(db, block, cond_name, "rdf:type",
                              "inv:ConditionalInvocation");
                insert_triple(db, block, frag_name,
                              "inv:hasConditionalInvocation", cond_name);

                const char *invocationName =
                    get_value(cond_inv, "inv:invocationName");
                if (invocationName)
                  insert_triple(db, block, cond_name, "inv:invocationName",
                                invocationName);

                cJSON *out_list =
                    cJSON_GetObjectItem(cond_inv, "inv:hasDestinationNames");
                if (out_list && cJSON_IsArray(out_list)) {
                  for (int j = 0; j < cJSON_GetArraySize(out_list); j++) {
                    cJSON *out = cJSON_GetArrayItem(out_list, j);
                    const char *out_name = get_value(out, "inv:name");
                    const char *out_content = get_value(out, "inv:content");

                    if (out_name) {
                      insert_triple(db, block, out_name, "rdf:type",
                                    "inv:DestinationPlace");
                      insert_triple(db, block, cond_name,
                                    "inv:hasDestinationNames", out_name);
                      insert_triple(db, block, out_name, "inv:name", out_name);
                      if (out_content)
                        insert_triple(db, block, out_name, "inv:content",
                                      out_content);
                    }
                  }
                }
              }
            }
          }
        }
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
