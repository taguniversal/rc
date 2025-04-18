#ifndef RDF_H
#define RDF_H

#include "cJSON.h"
#include <sqlite3.h>

int has_triple(sqlite3 *db, const char *block, const char *subj,
               const char *pred, const char *obj);
int has_predicate(sqlite3 *db, const char *block, const char *subj,
                  const char *pred);
void insert_triple(sqlite3 *db, const char *psi, const char *subj,
                   const char *pred, const char *obj);
void delete_triple(sqlite3 *db, const char *psi, const char *subject,
                   const char *predicate);
void delete_expression_triples(sqlite3 *db, const char *psi,
                               const char *expr_id);
//int load_rdf_from_dir(const char *dirname, sqlite3 *db, const char *psi);

char *lookup_object(sqlite3 *db, const char *block, const char *subject,
                    const char *predicate);

char *lookup_subject_by_name(sqlite3 *db, const char *block, const char *name);
int lookup_destinations_for_name(sqlite3 *db, const char *block,
                                 const char *name, char dests[][128],
                                 int max_dests);
int lookup_definition_io_list(
    sqlite3 *db, const char *block, const char *def_name,
    const char
        *direction_predicate, // "inv:SourceList" or "inv:DestinationList"
    char list[][128], int max_items);

// Fetch a raw string value (usually JSON) from a subject/predicate without
// parsing
char *lookup_raw_value(sqlite3 *db, const char *block, const char *subject,
                       const char *predicate);
#endif
