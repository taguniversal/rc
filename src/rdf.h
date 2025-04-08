#ifndef RDF_H
#define RDF_H

#include <sqlite3.h>
#include "cJSON.h"

void insert_triple(sqlite3 *db, const char *psi, const char *subj, const char *pred, const char *obj) ;
void delete_triple(sqlite3 *db, const char *subject, const char *predicate);
void delete_expression_triples(sqlite3* db, const char* psi, const char* expr_id);
int load_rdf_from_dir(const char *dirname, sqlite3 *db, const char *psi);
cJSON *query_time_series(sqlite3 *db, const char *subject, const char *key_prefix);
const char* lookup_object(sqlite3* db, const char* subject, const char* predicate);
const char* lookup_value_by_name(sqlite3* db, const char* expr_id, const char* name);
const char* lookup_subject_by_name(sqlite3* db, const char* name);
int lookup_destinations_for_name(sqlite3* db, const char* name, char dests[][128], int max_dests) ;
const char* lookup_resolution(sqlite3* db, const char* expr_id, const char* key);
// Find invocation subject ID by matching its inv:name
const char* find_invocation_by_name(sqlite3* db, const char* def_name);

// Fetch a raw string value (usually JSON) from a subject/predicate without parsing
char* lookup_raw_value(sqlite3* db, const char* subject, const char* predicate);

#endif
