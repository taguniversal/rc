#include "string_set.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

StringSet *create_string_set(void)
{
    StringSet *set = malloc(sizeof(StringSet));
    set->head = NULL;
    return set;
}

void destroy_string_set(StringSet *set)
{
    StringSetEntry *cur = set->head;
    while (cur)
    {
        StringSetEntry *next = cur->next;
        free(cur->key);
        free(cur);
        cur = next;
    }
    free(set);
}

void string_set_add_or_increment(StringSet *set, const char *key)
{
    for (StringSetEntry *cur = set->head; cur; cur = cur->next)
    {
        if (strcmp(cur->key, key) == 0)
        {
            cur->count += 1;
            return;
        }
    }

    // Not found; add new
    StringSetEntry *entry = malloc(sizeof(StringSetEntry));
    entry->key = strdup(key);
    entry->count = 1;
    entry->next = set->head;
    set->head = entry;
}

int string_set_count(StringSet *set, const char *key)
{
    for (StringSetEntry *cur = set->head; cur; cur = cur->next)
    {
        if (strcmp(cur->key, key) == 0)
            return cur->count;
    }
    return 0;
}
