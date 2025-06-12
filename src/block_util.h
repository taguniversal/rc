#ifndef BLOCK_UTIL_H
#define BLOCK_UTIL_H

#include "eval.h"

Definition *find_definition_by_name(Block *blk, const char *name);


void print_psi(const psi128_t *psi);
int parse_psi(const char *hex, psi128_t *out);
#endif
