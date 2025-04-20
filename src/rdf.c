#include "rdf.h"
#include "cJSON.h"
#include "log.h"
#include "mkrand.h"
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

// Returns 1 if found, 0 if not found
int lookup_definition_io_list(
    sqlite3 *db, const char *block, const char *def_name,
    const char
        *direction_predicate, // "inv:SourceList" or "inv:DestinationList"
    char list[][128], int max_items) {
  sqlite3_stmt *stmt;
  const char *sql =
      "SELECT s.subject FROM triples s "
      "WHERE s.psi = ? AND s.predicate = 'inv:name' AND s.object = ?";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ Failed to prepare definition lookup: %s", sqlite3_errmsg(db));
    return 0;
  }

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, def_name, -1, SQLITE_STATIC);

  int found = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && !found) {
    const char *subject = (const char *)sqlite3_column_text(stmt, 0);

    // Confirm it's a Definition
    sqlite3_stmt *check;
    const char *check_sql =
        "SELECT 1 FROM triples WHERE psi = ? AND subject = ? AND predicate = "
        "'rdf:type' AND object = 'inv:Definition'";

    if (sqlite3_prepare_v2(db, check_sql, -1, &check, NULL) == SQLITE_OK) {
      sqlite3_bind_text(check, 1, block, -1, SQLITE_STATIC);
      sqlite3_bind_text(check, 2, subject, -1, SQLITE_STATIC);

      if (sqlite3_step(check) == SQLITE_ROW) {
        sqlite3_finalize(check);

        // Now pull the SourceList or DestinationList
        sqlite3_stmt *io_stmt;
        const char *io_sql = "SELECT object FROM triples WHERE psi = ? AND "
                             "subject = ? AND predicate = ?";

        if (sqlite3_prepare_v2(db, io_sql, -1, &io_stmt, NULL) == SQLITE_OK) {
          sqlite3_bind_text(io_stmt, 1, block, -1, SQLITE_STATIC);
          sqlite3_bind_text(io_stmt, 2, subject, -1, SQLITE_STATIC);
          sqlite3_bind_text(io_stmt, 3, direction_predicate, -1, SQLITE_STATIC);

          int count = 0;
          while (sqlite3_step(io_stmt) == SQLITE_ROW && count < max_items) {
            const char *place = (const char *)sqlite3_column_text(io_stmt, 0);
            strncpy(list[count++], place, 128);
          }

          found = 1;
          sqlite3_finalize(io_stmt);
        }
      } else {
        sqlite3_finalize(check);
      }
    }
  }

  sqlite3_finalize(stmt);
  return found;
}

void dump_source_list(sqlite3 *db, const char *block, const char *def_name) {
  sqlite3_stmt *stmt;
  const char *sql = "SELECT object FROM triples WHERE psi = ? AND subject = ? AND predicate = 'inv:SourceList'";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return;

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, def_name, -1, SQLITE_STATIC);

  LOG_INFO("🔍 SourceList for %s:", def_name);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *src = (const char *)sqlite3_column_text(stmt, 0);
    LOG_INFO(" • %s", src);
  }

  sqlite3_finalize(stmt);
}


char *lookup_subject_by_object(sqlite3 *db, const char *block, const char *predicate, const char *object) {
  const char *sql = "SELECT subject FROM triples WHERE psi = ? AND predicate = ? AND object = ? LIMIT 1;";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ Failed to prepare subject lookup: %s\n", sqlite3_errmsg(db));
    return NULL;
  }

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, predicate, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, object, -1, SQLITE_STATIC);

  char *subject = NULL;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char *raw = sqlite3_column_text(stmt, 0);
    if (raw) subject = strdup((const char *)raw);
  }

  sqlite3_finalize(stmt);
  return subject;
}

// Caller is responsible for freeing the returned string.
char *lookup_subject_by_name(sqlite3 *db, const char *block, const char *name) {
  sqlite3_stmt *stmt;
  const char *sql = "SELECT subject FROM triples WHERE psi = ? AND predicate = "
                    "'inv:name' AND object = ?";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("Failed to prepare lookup_subject_by_name query.");
    return NULL;
  }

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);

  char *subject = NULL;

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *text = (const char *)sqlite3_column_text(stmt, 0);
    if (text) {
      subject = strdup(text); // Caller must free
    }
  }

  sqlite3_finalize(stmt);
  return subject;
}

int lookup_destinations_for_name(sqlite3 *db, const char *block,
                                 const char *name, char dests[][128],
                                 int max_dests) {
  sqlite3_stmt *stmt;
  const char *sql =
      "SELECT DISTINCT s.subject FROM triples s "
      "WHERE s.psi = ? AND s.predicate = 'inv:name' AND s.object = ?;";
  int count = 0;

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return 0;

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);

  while (sqlite3_step(stmt) == SQLITE_ROW && count < max_dests) {
    const char *subject = (const char *)sqlite3_column_text(stmt, 0);

    // Verify it's a DestinationPlace within the same block
    sqlite3_stmt *stmt2;
    const char *check_type =
        "SELECT 1 FROM triples WHERE psi = ? AND subject = ? "
        "AND predicate = 'rdf:type' AND object = 'inv:DestinationPlace';";

    if (sqlite3_prepare_v2(db, check_type, -1, &stmt2, NULL) == SQLITE_OK) {
      sqlite3_bind_text(stmt2, 1, block, -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt2, 2, subject, -1, SQLITE_STATIC);

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

char *lookup_raw_value(sqlite3 *db, const char *block, const char *subject,
                       const char *predicate) {
  sqlite3_stmt *stmt;
  const char *sql = "SELECT object FROM triples WHERE psi = ? AND subject = ? "
                    "AND predicate = ? LIMIT 1";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ Failed to prepare lookup_raw_value query: %s\n",
              sqlite3_errmsg(db));
    return NULL;
  }

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, subject, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, predicate, -1, SQLITE_STATIC);

  char *result = NULL;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *val = (const char *)sqlite3_column_text(stmt, 0);
    if (val)
      result = strdup(val); // strdup once is enough here
  }

  sqlite3_finalize(stmt);
  return result;
}

// Caller is responsible for freeing the returned string.
char *lookup_object(sqlite3 *db, const char *block, const char *subject,
                    const char *predicate) {
  sqlite3_stmt *stmt;
  LOG_INFO("RDF lookup_object. Subject: %s, Predicate: %s\n", subject,
           predicate);

  const char *sql = "SELECT object FROM triples WHERE psi = ? AND subject = ? "
                    "AND predicate = ? LIMIT 1;";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return NULL;

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, subject, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, predicate, -1, SQLITE_STATIC);

  char *copy = NULL;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *text = (const char *)sqlite3_column_text(stmt, 0);
    if (text) {
      copy = strdup(text); // caller must free
    }
  }

  sqlite3_finalize(stmt);
  return copy;
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
                   const char *pred, const char *value) {
  sqlite3_stmt *stmt;

  const char *sql = "REPLACE INTO triples (psi, subject, predicate, object) "
                    "VALUES (?, ?, ?, ?);";

  LOG_INFO("📝 Inserting: [%s] %s -- %s --> %s", psi, subject, pred, value);

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ Failed to prepare insert_triple\n");
    return;
  }

  sqlite3_bind_text(stmt, 1, psi, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, subject, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, pred, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, value, -1, SQLITE_STATIC);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    LOG_ERROR("❌ Failed to execute insert_triple\n");
  }

  sqlite3_finalize(stmt);
}

int has_triple(sqlite3 *db, const char *block, const char *subj,
               const char *pred, const char *obj) {
  sqlite3_stmt *stmt;
  const char *sql = "SELECT 1 FROM triples WHERE psi = ? AND subject = ? AND "
                    "predicate = ? AND object = ? LIMIT 1";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return 0;
  }

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, subj, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, pred, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, obj, -1, SQLITE_STATIC);

  int exists = (sqlite3_step(stmt) == SQLITE_ROW);
  sqlite3_finalize(stmt);
  return exists;
}

int has_predicate(sqlite3 *db, const char *block, const char *subj,
                  const char *pred) {
  sqlite3_stmt *stmt;
  const char *sql = "SELECT 1 FROM triples WHERE psi = ? AND subject = ? AND "
                    "predicate = ? LIMIT 1";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ has_predicate: Failed to prepare statement: %s\n",
              sqlite3_errmsg(db));
    return 0;
  }

  sqlite3_bind_text(stmt, 1, block, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, subj, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, pred, -1, SQLITE_STATIC);

  int found = (sqlite3_step(stmt) == SQLITE_ROW);
  sqlite3_finalize(stmt);
  return found;
}

void delete_triple(sqlite3 *db, const char *psi, const char *subject,
                   const char *predicate) {
  sqlite3_stmt *stmt;
  const char *sql =
      "DELETE FROM triples WHERE psi = ? AND subject = ? AND predicate = ?;";

  LOG_INFO("delete_triple start, subject: %s, predicate: %s, psi: %s\n",
           subject, predicate, psi);

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, psi, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, subject, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, predicate, -1, SQLITE_STATIC);

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

