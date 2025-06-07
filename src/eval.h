#ifndef EVAL_H
#define EVAL_H
#include "block.h"
#include "sqlite3.h"
#include <stdint.h>
#include <ctype.h>
#include <stddef.h> // for size_t
#include <netinet/in.h> // For in6_addr
#define SAFETY_GUARD



int eval(Block *blk);

#endif
