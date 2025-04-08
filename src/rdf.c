#include "rdf.h"
#include "cJSON.h"
#include "log.h"
#include <dirent.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PSI_BLOCK_LEN 39
#define OSC_PORT_XMIT 4242
#define OSC_PORT_RECV 4243
#define BUFFER_SIZE 1024
#define IPV6_ADDR "fc00::1"

const char *lookup_value_by_name(sqlite3 *db, const char *expr_id,
                                 const char *name) {
  sqlite3_stmt *stmt;
  const char *sql = "SELECT subject FROM triples WHERE subject IN ("
                    "SELECT object FROM triples WHERE subject = ? AND "
                    "predicate = 'inv:source_list') "
                    "AND predicate = 'inv:name' AND object = ?";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("Failed to prepare lookup_value_by_name query.");
    return NULL;
  }

  sqlite3_bind_text(stmt, 1, expr_id, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);

  const char *source_id = NULL;

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    source_id = strdup((const char *)sqlite3_column_text(
        stmt, 0)); // Need to copy it because sqlite3_finalize will free it
  }

  sqlite3_finalize(stmt);

  if (!source_id)
    return NULL;

  const char *value = lookup_object(db, source_id, "inv:hasContent");
  free((void *)source_id);
  return value;
}

const char *lookup_subject_by_name(sqlite3 *db, const char *name) {
  sqlite3_stmt *stmt;
  const char *sql =
      "SELECT subject FROM triples WHERE predicate = 'inv:name' AND object = ?";
  sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
  const char *subject = NULL;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    subject = strdup((const char *)sqlite3_column_text(stmt, 0));
  }
  sqlite3_finalize(stmt);
  return subject;
}

int lookup_destinations_for_name(sqlite3 *db, const char *name,
                                 char dests[][128], int max_dests) {
  sqlite3_stmt *stmt;
  const char *sql = "SELECT DISTINCT s.subject FROM triples s \
                   WHERE s.predicate = 'inv:name' AND s.object = ?;";
  int count = 0;

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return 0;
  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

  while (sqlite3_step(stmt) == SQLITE_ROW && count < max_dests) {
    const char *subject = (const char *)sqlite3_column_text(stmt, 0);

    // Verify it's a DestinationPlace
    sqlite3_stmt *stmt2;
    const char *check_type =
        "SELECT 1 FROM triples WHERE subject = ? AND predicate = 'rdf:type' "
        "AND object = 'inv:DestinationPlace';";
    if (sqlite3_prepare_v2(db, check_type, -1, &stmt2, NULL) == SQLITE_OK) {
      sqlite3_bind_text(stmt2, 1, subject, -1, SQLITE_STATIC);
      if (sqlite3_step(stmt2) == SQLITE_ROW) {
        strncpy(dests[count], subject, 128);
        count++;
      }
      sqlite3_finalize(stmt2);
    }
  }

  sqlite3_finalize(stmt);
  return count;
}

const char *lookup_resolution(sqlite3 *db, const char *expr_id,
                              const char *key) {
  LOG_INFO("🔍 Starting resolution lookup in %s for key %s\n", expr_id, key);

  sqlite3_stmt *stmt;
  const char *sql = "SELECT object FROM triples WHERE subject = ? AND "
                    "predicate = 'inv:ContainsDefinition'";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ Failed to query ContainsDefinition: %s\n",
              sqlite3_errmsg(db));
    return NULL;
  }

  sqlite3_bind_text(stmt, 1, expr_id, -1, SQLITE_STATIC);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *def_id = (const char *)sqlite3_column_text(stmt, 0);
    LOG_INFO("🔍 Found contained definition: %s\n", def_id);

    // Now get resolutionTable[key]
    const char *result = lookup_object(db, def_id, key);
    if (result) {
      LOG_INFO("✅ Found resolution: %s => %s\n", key, result);
      sqlite3_finalize(stmt);
      return result;
    } else {
      LOG_WARN("🛑 No match for key %s in def %s\n", key, def_id);
    }
  }

  sqlite3_finalize(stmt);
  return NULL;
}

const char *find_invocation_by_name(sqlite3 *db, const char *def_name) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT subject FROM triples WHERE predicate = 'inv:name' AND object = ? AND subject IN ("
        "SELECT subject FROM triples WHERE predicate = 'rdf:type' AND object = 'inv:Invocation') LIMIT 1";
  
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      LOG_ERROR("❌ Failed to prepare find_invocation_by_name query: %s\n", sqlite3_errmsg(db));
      return NULL;
    }
  
    sqlite3_bind_text(stmt, 1, def_name, -1, SQLITE_STATIC);
  
    const char *result = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      result = strdup((const char *)sqlite3_column_text(stmt, 0));
    }
  
    sqlite3_finalize(stmt);
    return result;
  }

  char *lookup_raw_value(sqlite3 *db, const char *subject, const char *predicate) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT object FROM triples WHERE subject = ? AND predicate = ? LIMIT 1";
  
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      LOG_ERROR("❌ Failed to prepare lookup_raw_value query: %s\n", sqlite3_errmsg(db));
      return NULL;
    }
  
    sqlite3_bind_text(stmt, 1, subject, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, predicate, -1, SQLITE_STATIC);
  
    char *result = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      const char *val = (const char *)sqlite3_column_text(stmt, 0);
      if (val) result = strdup(val);
    }
  
    sqlite3_finalize(stmt);
    return result;
  }
  

const char *lookup_object(sqlite3 *db, const char *subject,
                          const char *predicate) {
  static char result[256];
  sqlite3_stmt *stmt;
  LOG_INFO("RDF lookup_object. Subject: %s, Predicate: %s\n", subject,
           predicate);

  const char *sql =
      "SELECT object FROM triples WHERE subject = ? AND predicate = ? LIMIT 1;";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return NULL;

  sqlite3_bind_text(stmt, 1, subject, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, predicate, -1, SQLITE_STATIC);

  const char *found = NULL;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    strncpy(result, (const char *)sqlite3_column_text(stmt, 0), sizeof(result));
    result[sizeof(result) - 1] = '\0';
    found = result;
  }

  sqlite3_finalize(stmt);
  return found;
}

void delete_expression_triples(sqlite3 *db, const char *psi,
                               const char *expr_id) {
  sqlite3_stmt *stmt;
  const char *sql = "DELETE FROM triples WHERE psi = ? AND subject = ?";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, psi, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, expr_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      fprintf(stderr, "❌ SQLite Error (delete): %s\n", sqlite3_errmsg(db));
    } else {
      LOG_INFO("🧹 Deleted existing triples for %s in block %s\n", expr_id,
               psi);
    }

    sqlite3_finalize(stmt);
  } else {
    fprintf(stderr, "❌ SQLite Error (prepare delete): %s\n",
            sqlite3_errmsg(db));
  }
}

void insert_triple(sqlite3 *db, const char *psi, const char *subject,
                   const char *predicate, const char *object) {
  sqlite3_stmt *stmt;
  const char *sql = "INSERT OR IGNORE INTO triples (psi, subject, predicate, "
                    "object) VALUES (?, ?, ?, ?);";

  LOG_INFO("insert_triple start, subject: %s, predicate: %s, object: %s\n",
           subject, predicate, object);
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, psi, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, subject, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, predicate, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, object, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_CONSTRAINT) {
      LOG_ERROR("❌ SQLite Error (step): %s\n", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
  } else {
    LOG_ERROR("❌ SQLite Error (prepare): %s\n", sqlite3_errmsg(db));
  }
}

void delete_triple(sqlite3 *db, const char *subject, const char *predicate) {
  sqlite3_stmt *stmt;
  const char *sql = "DELETE FROM triples WHERE subject = ? AND predicate = ?;";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, subject, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, predicate, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      fprintf(stderr, "❌ SQLite Error (delete step): %s\n",
              sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
  } else {
    fprintf(stderr, "❌ SQLite Error (delete prepare): %s\n",
            sqlite3_errmsg(db));
  }
}

static void load_jsonld_file(const char *path, sqlite3 *db, const char *psi) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
  
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
  
    char *data = malloc(len + 1);
    fread(data, 1, len, f);
    data[len] = '\0';
    fclose(f);
  
    cJSON *json = cJSON_Parse(data);
    free(data);
    if (!json) return;
  
    // 🔤 Use filename (without .json) as subject name
    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
  
    char subject[256];
    strncpy(subject, filename, sizeof(subject));
    subject[sizeof(subject) - 1] = '\0';
    char *dot = strrchr(subject, '.');
    if (dot) *dot = '\0';  // Strip .json
  
    // 🏷️ Process @type
    cJSON *types = cJSON_GetObjectItem(json, "@type");
    if (cJSON_IsArray(types)) {
      cJSON *t;
      cJSON_ArrayForEach(t, types) {
        insert_triple(db, psi, subject, "rdf:type", t->valuestring);
      }
    } else if (cJSON_IsString(types)) {
      insert_triple(db, psi, subject, "rdf:type", types->valuestring);
    }
  
    // 🧠 Main loader logic
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, json) {
      if (strcmp(item->string, "@type") == 0) continue;
  
      // Handle I/O shortcut
      if (strcmp(item->string, "inv:IO") == 0 && cJSON_IsObject(item)) {
        cJSON *inputs = cJSON_GetObjectItem(item, "inputs");
        if (inputs && cJSON_IsArray(inputs)) {
          cJSON *name = NULL;
          cJSON_ArrayForEach(name, inputs) {
            if (!cJSON_IsString(name)) continue;
            const char *pname = name->valuestring;
            insert_triple(db, psi, pname, "rdf:type", "inv:SourcePlace");
            insert_triple(db, psi, pname, "inv:name", pname);
            insert_triple(db, psi, subject, "inv:SourceList", pname);
          }
        }
  
        cJSON *outputs = cJSON_GetObjectItem(item, "inv:Outputs");
        if (outputs && cJSON_IsArray(outputs)) {
          cJSON *name = NULL;
          cJSON_ArrayForEach(name, outputs) {
            if (!cJSON_IsString(name)) continue;
            const char *pname = name->valuestring;
            insert_triple(db, psi, pname, "rdf:type", "inv:DestinationPlace");
            insert_triple(db, psi, pname, "inv:name", pname);
            insert_triple(db, psi, subject, "inv:DestinationList", pname);
          }
        }
  
        continue;  // Skip this item
      }
  
      // Generic field handler
      if (cJSON_IsString(item)) {
        insert_triple(db, psi, subject, item->string, item->valuestring);
      }
    }
  
    cJSON_Delete(json);
  }
  

int load_rdf_from_dir(const char *dirname, sqlite3 *db, const char *psi) {
  DIR *dir = opendir(dirname);
  if (!dir)
    return -1;

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strstr(entry->d_name, ".json")) {
      char fullpath[512];
      snprintf(fullpath, sizeof(fullpath), "%s/%s", dirname, entry->d_name);
      load_jsonld_file(fullpath, db, psi);
    }
  }

  closedir(dir);
  return 0;
}
cJSON *query_time_series(sqlite3 *db, const char *subject,
                         const char *predicate) {
  const char *sql =
      "SELECT t1.object, t2.object FROM triples t1 "
      "JOIN triples t2 ON t1.psi = t2.psi "
      "WHERE t1.subject = ? AND t1.predicate = ? "
      "AND t2.predicate = 'timestamp' AND t2.subject = t1.subject "
      "ORDER BY t2.object ASC "
      "LIMIT 100;";

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    fprintf(stderr, "❌ Failed to prepare query: %s\n", sqlite3_errmsg(db));
    return NULL;
  }

  sqlite3_bind_text(stmt, 1, subject, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, predicate, -1, SQLITE_STATIC);
  printf("🔍 Looking for subject = %s, predicate = %s\n", subject, predicate);

  cJSON *series = cJSON_CreateArray();
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *value = (const char *)sqlite3_column_text(stmt, 0);
    const char *timestamp = (const char *)sqlite3_column_text(stmt, 1);

    if (value && timestamp) {
      cJSON *point = cJSON_CreateObject();
      cJSON_AddStringToObject(point, "value", value);
      cJSON_AddStringToObject(point, "timestamp", timestamp);
      cJSON_AddItemToArray(series, point);
    }
  }

  sqlite3_finalize(stmt);

  printf("📈 Time Series JSON: %s\n", cJSON_Print(series));
  return series;
}
