#ifndef RDF_H
#define RDF_H

#include "cJSON.h"
#include <sqlite3.h>

void insert_triple(sqlite3 *db, const char *psi, const char *subj,
                   const char *pred, const char *obj);
void delete_triple(sqlite3 *db, const char *psi, const char *subject,
                   const char *predicate);
void delete_expression_triples(sqlite3 *db, const char *psi,
                               const char *expr_id);
int load_rdf_from_dir(const char *dirname, sqlite3 *db, const char *psi);
cJSON *query_time_series(sqlite3 *db, const char *subject,
                         const char *key_prefix);
char *lookup_object(sqlite3 *db, const char *block, const char *subject, const char *predicate);
 
char *lookup_subject_by_name(sqlite3 *db, const char *block, const char *name);
int lookup_destinations_for_name(sqlite3 *db, const char *block,
                                 const char *name, char dests[][128],
                                 int max_dests);
// Find invocation subject ID by matching its inv:name
char *find_invocation_by_name(sqlite3 *db, const char *block,
                              const char *def_name);

// Fetch a raw string value (usually JSON) from a subject/predicate without
// parsing
char *lookup_raw_value(sqlite3 *db, const char *block, const char *subject,
                       const char *predicate);
#endif
