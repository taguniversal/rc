#ifndef WIRING_H
#define WIRING_H
#include <stdio.h>
#include <sqlite3.h>
#include "eval.h"

void dump_wiring(Block* blk);
void dump_all_signal_states(sqlite3 *db, const char *block);
void export_graph_dot(sqlite3 *db, const char *block, FILE *out);
void write_debug_html(sqlite3 *db, const char *block, const char *filename);
void write_graph_dot_file(sqlite3 *db, const char *block, const char *filename);
#endif
