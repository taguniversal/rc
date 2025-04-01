#ifndef RDF_H
#define RDF_H

#include <sqlite3.h>
#include "cJSON.h"

void insert_triple(sqlite3 *db, const char *psi, const char *subj, const char *pred, const char *obj) ;
int load_rdf_from_dir(const char *dirname, sqlite3 *db, const char *psi);
cJSON *query_time_series(sqlite3 *db, const char *subject, const char *key_prefix);
const char* lookup_object(sqlite3* db, const char* subject, const char* predicate);
const char* lookup_value(sqlite3* db, const char* subject, const char* predicate);

#endif
