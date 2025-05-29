
#include "eval.h"
#include <stdio.h>
#include <string.h>

Definition *find_definition_by_name(Block *blk, const char *name)
{
    if (!blk || !name)
        return NULL;

    for (Definition *def = blk->definitions; def; def = def->next)
    {
        if (def->name && strcmp(def->name, name) == 0)
        {
            return def;
        }
    }

    return NULL; // Not found
}


SourcePlace *find_source(Block *blk, const char *global_name)
{
    for (size_t i = 0; i < blk->sources.count; ++i) {
        SourcePlace *src = blk->sources.items[i];
        if (!src || !src->resolved_name)
            continue;

        if (strcmp(src->resolved_name, global_name) == 0)
            return src;
    }
    return NULL;
}

DestinationPlace *find_destination(Block *blk, const char *global_name)
{
    for (size_t i = 0; i < blk->destinations.count; ++i) {
        DestinationPlace *dst = blk->destinations.items[i];
        if (!dst || !dst->resolved_name)
            continue;

        if (strcmp(dst->resolved_name, global_name) == 0)
            return dst;
    }
    return NULL;
}
