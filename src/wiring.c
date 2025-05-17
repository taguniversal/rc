#include "wiring.h"
#include "log.h"
#include "eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eval.h"
#include "log.h"  // or whatever your logging header is called

void dump_wiring(Block *blk)
{
  LOG_INFO("🧪 Dumping wiring for all invocations in block PSI: %s", blk->psi);

  Invocation *inv = blk->invocations;
  while (inv)
  {
    LOG_INFO("  🔽 Invocation: %s", inv->name);

    SourcePlace *src = inv->sources;
    while (src)
    {
      const char *sigval = (src->signal && src->signal->content) ? src->signal->content : "(null)";
      LOG_INFO("    ➤ Source: %-12s → Signal: %p (%s)", src->name, (void *)src->signal, sigval);
      src = src->next;
    }

    DestinationPlace *dst = inv->destinations;
    while (dst)
    {
      const char *sigval = (dst->signal && dst->signal->content) ? dst->signal->content : "(null)";
      LOG_INFO("    ➤ Dest:   %-12s → Signal: %p (%s)", dst->name, (void *)dst->signal, sigval);
      dst = dst->next;
    }

    inv = inv->next;
  }
}


void export_graph_dot(sqlite3 *db, const char *block, FILE *out)
{
  fprintf(out, "digraph G {\n");
  fprintf(out, "  rankdir=LR;\n");
  fprintf(out, "  node [shape=box];\n");

  fprintf(out, "}\n");
}

void write_debug_html(sqlite3 *db, const char *block, const char *filename)
{
  FILE *f = fopen(filename, "w");
  if (!f)
    return;
  fclose(f);
}
