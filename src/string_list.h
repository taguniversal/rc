#ifndef STRING_LIST_H
#define STRING_LIST_H

#include <stddef.h>

typedef struct StringListEntry {
    char *key;
    struct StringListEntry *next;
} StringListEntry;

typedef struct {
    StringListEntry *head;
    StringListEntry *tail;
    size_t size;
} StringList;

StringList *create_string_list(void);
void destroy_string_list(StringList *set);
void string_list_add(StringList *set, const char *key);
const char *string_list_get_by_index(StringList *set, size_t index);
void string_list_set_by_index(StringList *set, size_t index, const char *new_value);
int string_list_contains(StringList *set, const char *key);
size_t string_list_count(StringList *set);


#endif
