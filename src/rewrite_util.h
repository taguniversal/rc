#ifndef REWRITE_UTIL_H
#define REWRITE_UTIL_H

#include "eval.h" 

//void rewrite_invocations(Block *blk);
//void rewrite_definition_signals(Definition *def);
//void rewrite_conditional_invocation(Definition *def);
int qualify_local_signals(Block *blk);
void cleanup_name_counters(void);
int get_next_instance_id(const char *name);

typedef struct NameCounter {
  const char *name;
  int count;
  struct NameCounter *next;
} NameCounter;

extern NameCounter *counters;


#endif // REWRITE_UTIL_H
