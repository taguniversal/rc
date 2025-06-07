#ifndef BLOCK_UTIL_H
#define BLOCK_UTIL_H

#include "eval.h"

Definition *find_definition_by_name(Block *blk, const char *name);
DestinationPlace *find_destination(Block *blk, const char *global_name);
SourcePlace *find_source(Block *blk, const char *global_name);
DestinationPlace *find_definition_output(Definition *def, const char *resolved_name) ;

void print_psi(psi128_t *psi);
int parse_psi(const char *hex, psi128_t *out);
#endif
