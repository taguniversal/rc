#include "string_list.h"
#include <stdlib.h>
#include <string.h>

StringList *create_string_list(void)
{
    StringList *set = malloc(sizeof(StringList));
    set->head = NULL;
    set->tail = NULL;
    set->size = 0;
    return set;
}

void destroy_string_list(StringList *set)
{
    StringListEntry *cur = set->head;
    while (cur)
    {
        StringListEntry *next = cur->next;
        free(cur->key);
        free(cur);
        cur = next;
    }
    free(set);
}

size_t string_list_count(StringList *set)
{
    return set ? set->size : 0;
}

int string_list_contains(StringList *set, const char *key)
{
    for (StringListEntry *cur = set->head; cur; cur = cur->next)
    {
        if (strcmp(cur->key, key) == 0)
            return 1;
    }
    return 0;
}

void string_list_add(StringList *string_list, const char *key)
{
    if (string_list_contains(string_list, key))
        return;

    StringListEntry *entry = malloc(sizeof(StringListEntry));
    entry->key = strdup(key);
    entry->next = NULL;

    if (!string_list->head)
        string_list->head = entry;
    else
        string_list->tail->next = entry;

    string_list->tail = entry;
    string_list->size += 1;
}

char *string_list_get_by_index(StringList *string_list, size_t index)
{
    if (!string_list || index >= string_list->size)
        return NULL;

    size_t i = 0;
    for (StringListEntry *cur = string_list->head; cur; cur = cur->next)
    {
        if (i++ == index)
            return strdup(cur->key);  // 🔐 Safe copy for caller
    }
    return NULL;
}

void string_list_set_by_index(StringList *list, size_t index, const char *new_value)
{
    if (!list || index >= list->size || !new_value)
        return;

    size_t i = 0;
    for (StringListEntry *cur = list->head; cur; cur = cur->next)
    {
        if (i++ == index)
        {
            // Copy new_value *before* freeing old one
            char *copy = strdup(new_value);
            if (!copy) return;

            free(cur->key);  // free after safe copy
            cur->key = copy;
            return;
        }
    }
}
