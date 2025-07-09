
#ifndef EVAL_UTIL_H
#define EVAL_UTIL_H
#include "invocation.h"
#include "eval.h"
#include "instance.h"
#include <stdbool.h>


void publish_all_literal_bindings(Block *blk, SignalMap* signal_map) ;

void unify_invocations(Block *blk, SignalMap* signal_map);

void dump_literal_bindings(Invocation *inv);

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#endif
