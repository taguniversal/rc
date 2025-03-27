#include "rdf.h"
#include "cJSON.h"
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>

void insert_triple(sqlite3 *db, const char *psi, const char *subject, const char *predicate, const char *object) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR IGNORE INTO triples (psi, subject, predicate, object) VALUES (?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, psi, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, subject, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, predicate, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, object, -1, SQLITE_STATIC);

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_CONSTRAINT) {
            fprintf(stderr, "❌ SQLite Error (step): %s\n", sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    } else {
        fprintf(stderr, "❌ SQLite Error (prepare): %s\n", sqlite3_errmsg(db));
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

    const char *subject = cJSON_GetObjectItem(json, "@id")->valuestring;
    cJSON *types = cJSON_GetObjectItem(json, "@type");
    if (cJSON_IsArray(types)) {
        cJSON *t;
        cJSON_ArrayForEach(t, types) {
            insert_triple(db, psi, subject, "rdf:type", t->valuestring);
        }
    } else if (cJSON_IsString(types)) {
        insert_triple(db, psi, subject, "rdf:type", types->valuestring);
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, json) {
        if (strcmp(item->string, "@id") == 0 || strcmp(item->string, "@type") == 0)
            continue;

        if (cJSON_IsObject(item)) {
            cJSON *x = cJSON_GetObjectItem(item, "x");
            cJSON *y = cJSON_GetObjectItem(item, "y");
            if (x) insert_triple(db, psi, subject, item->string, x->valuestring);
            if (y) insert_triple(db, psi, subject, item->string, y->valuestring);
        } else if (cJSON_IsString(item)) {
            insert_triple(db, psi, subject, item->string, item->valuestring);
        }
    }

    cJSON_Delete(json);
}

int load_rdf_from_dir(const char *dirname, sqlite3 *db, const char *psi) {
    DIR *dir = opendir(dirname);
    if (!dir) return -1;

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
cJSON *query_time_series(sqlite3 *db, const char *subject, const char *predicate) {
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

