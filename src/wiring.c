#include "wiring.h"
#include "log.h"
#include "rdf.h"
#include "eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void dump_wiring(Block* blk) {
  LOG_INFO("🐐 GOAT Diagram: Signal Wiring");

  LOG_INFO("🟢 Done.");
}


void export_graph_dot(sqlite3 *db, const char *block, FILE *out) {
  fprintf(out, "digraph G {\n");
  fprintf(out, "  rankdir=LR;\n");
  fprintf(out, "  node [shape=box];\n");

  fprintf(out, "}\n");
}


void write_debug_html(sqlite3 *db, const char *block, const char *filename) {
  FILE *f = fopen(filename, "w");
  if (!f)
    return;
  fclose(f);
}
