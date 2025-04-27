#include "rdf.h"
#include "log.h"
#include "mkrand.h"
#include "eval.h"
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

// Returns a strdup()'d subject for a Definition with a given name,
// or NULL if not found. Caller must free the result.
char *lookup_definition_subject_by_name(sqlite3 *db, const char *block, const char *name) {
  sqlite3_stmt *stmt;
  const char *sql =
    "SELECT a.subject "
    "FROM triples a "
    "JOIN triples b ON a.subject = b.subject "
    "WHERE a.predicate = 'inv:name' AND a.object = ?1 "
    "  AND b.predicate = 'rdf:type' AND b.object = 'inv:Definition';";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ Failed to prepare definition lookup query: %s\n", sqlite3_errmsg(db));
    return NULL;
  }

  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);

  char *result = NULL;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char *text = sqlite3_column_text(stmt, 0);
    result = strdup((const char *)text);
    LOG_INFO("🔎 Found definition subject for '%s': %s\n", name, result);
  } else {
    LOG_WARN("⚠️ No matching definition subject found for: %s\n", name);
  }

  sqlite3_finalize(stmt);
  return result;
}
void drop_old_triples(sqlite3 *db, Block *blk) {
  if (!blk || !blk->psi) {
      LOG_WARN("⚠️ drop_old_triples called with NULL block or psi");
      return;
  }

  char sql_delete[256];
  snprintf(sql_delete, sizeof(sql_delete),
           "DELETE FROM triples WHERE psi != '%s';", blk->psi);

  LOG_INFO("💣 Dropping all triples not in block %s", blk->psi);

  char *errmsg = NULL;
  int rc = sqlite3_exec(db, sql_delete, NULL, NULL, &errmsg);
  if (rc != SQLITE_OK) {
      LOG_ERROR("❌ Failed to delete old triples: %s", errmsg);
      sqlite3_free(errmsg);
  }
}



char *lookup_raw_value(sqlite3 *db, Block *blk, const char *subject,
                       const char *predicate) {
  sqlite3_stmt *stmt;
  const char *sql = "SELECT object FROM triples WHERE psi = ? AND subject = ? "
                    "AND predicate = ? LIMIT 1";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ Failed to prepare lookup_raw_value query: %s\n",
              sqlite3_errmsg(db));
    return NULL;
  }

  sqlite3_bind_text(stmt, 1, blk->psi, -1, SQLITE_STATIC);
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
char *lookup_object(sqlite3 *db, Block* blk, const char *subject,
                    const char *predicate) {
  sqlite3_stmt *stmt;
  LOG_INFO("RDF lookup_object. Subject: %s, Predicate: %s\n", subject,
           predicate);

  const char *sql = "SELECT object FROM triples WHERE psi = ? AND subject = ? "
                    "AND predicate = ? LIMIT 1;";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return NULL;

  sqlite3_bind_text(stmt, 1, blk->psi, -1, SQLITE_STATIC);
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


void insert_triple(sqlite3 *db, Block* blk, const char *subject,
                   const char *pred, const char *value) {
  sqlite3_stmt *stmt;
  const char* psi = blk->psi;
  if (!psi || strlen(psi) == 0) {
    LOG_WARN("⚠️  insert_triple() called with NULL or empty psi! subject=%s predicate=%s object=%s",
             subject, pred, value);
  }

  const char *sql = "REPLACE INTO triples (psi, subject, predicate, object) "
                    "VALUES (?, ?, ?, ?);";

  LOG_INFO("📝 Inserting: %s %s -- %s --> %s", psi, subject, pred, value);

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

int has_triple(sqlite3 *db, Block* blk, const char *subj,
               const char *pred, const char *obj) {
  sqlite3_stmt *stmt;
  const char *sql = "SELECT 1 FROM triples WHERE psi = ? AND subject = ? AND "
                    "predicate = ? AND object = ? LIMIT 1";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return 0;
  }

  sqlite3_bind_text(stmt, 1, blk->psi, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, subj, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, pred, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, obj, -1, SQLITE_STATIC);

  int exists = (sqlite3_step(stmt) == SQLITE_ROW);
  sqlite3_finalize(stmt);
  return exists;
}

int has_predicate(sqlite3 *db, Block* blk, const char *subj,
                  const char *pred) {
  sqlite3_stmt *stmt;
  const char *sql = "SELECT 1 FROM triples WHERE psi = ? AND subject = ? AND "
                    "predicate = ? LIMIT 1";

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    LOG_ERROR("❌ has_predicate: Failed to prepare statement: %s\n",
              sqlite3_errmsg(db));
    return 0;
  }

  sqlite3_bind_text(stmt, 1, blk->psi, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, subj, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, pred, -1, SQLITE_STATIC);

  int found = (sqlite3_step(stmt) == SQLITE_ROW);
  sqlite3_finalize(stmt);
  return found;
}

void delete_triple(sqlite3 *db, Block* blk, const char *subject,
                   const char *predicate) {
  sqlite3_stmt *stmt;
  const char *sql =
      "DELETE FROM triples WHERE psi = ? AND subject = ? AND predicate = ?;";

  LOG_INFO("delete_triple start, subject: %s, predicate: %s, psi: %s\n",
           subject, predicate, blk->psi);

  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, blk->psi, -1, SQLITE_STATIC);
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

