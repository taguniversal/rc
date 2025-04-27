#ifndef RDF_H
#define RDF_H

#include <sqlite3.h>
#include "eval.h"

int has_triple(sqlite3 *db, Block *blk, const char *subj,
               const char *pred, const char *obj);
int has_predicate(sqlite3 *db,  Block *blk, const char *subj,
                  const char *pred);
void insert_triple(sqlite3 *db, Block* blk, const char *subj,
                   const char *pred, const char *obj);
void delete_triple(sqlite3 *db, Block* blk, const char *subject,
                    const char *predicate);

char *lookup_object(sqlite3 *db, Block *blk, const char *subject,
                    const char *predicate);


void drop_old_triples(sqlite3 *db, Block *blk);

#endif
