#ifndef REWRITE_UTIL_H
#define REWRITE_UTIL_H

#include "eval.h" 

void rewrite_top_level_invocations(Block *blk);
void rewrite_definition_signals(Definition *def);
void rewrite_por_invocations(Definition *def);
void rewrite_conditional_invocation(Definition *def);
void wire_por_outputs_to_sources(Definition *def);
void wire_conditional_results_to_invocation_sources(Block *blk);
void wire_por_sources_to_outputs(Definition *def);
void wire_invocation_sources_to_definition_outputs(Block *blk);
void cleanup_name_counters(void);
int get_next_instance_id(const char *name);

typedef struct NameCounter {
  const char *name;
  int count;
  struct NameCounter *next;
} NameCounter;

extern NameCounter *counters;


#endif // REWRITE_UTIL_H
