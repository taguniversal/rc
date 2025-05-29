#ifndef BLOCK_UTIL_H
#define BLOCK_UTIL_H

#include "eval.h"

Definition *find_definition_by_name(Block *blk, const char *name);
DestinationPlace *find_destination(Block *blk, const char *global_name);
SourcePlace *find_source(Block *blk, const char *global_name);

#endif
