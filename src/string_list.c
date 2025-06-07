#include "string_list.h"
#include <stdlib.h>
#include <string.h>

StringList *create_string_set(void) {
    StringList *set = malloc(sizeof(StringList));
    set->head = NULL;
    set->tail = NULL;
    set->size = 0;
    return set;
}

void destroy_string_set(StringList *set) {
    StringListEntry *cur = set->head;
    while (cur) {
        StringListEntry *next = cur->next;
        free(cur->key);
        free(cur);
        cur = next;
    }
    free(set);
}

size_t string_set_count(StringList *set) {
    return set ? set->size : 0;
}

int string_set_contains(StringList *set, const char *key) {
    for (StringListEntry *cur = set->head; cur; cur = cur->next) {
        if (strcmp(cur->key, key) == 0)
            return 1;
    }
    return 0;
}

void string_set_add(StringList *set, const char *key) {
    if (string_set_contains(set, key)) return;

    StringListEntry *entry = malloc(sizeof(StringListEntry));
    entry->key = strdup(key);
    entry->next = NULL;

    if (!set->head)
        set->head = entry;
    else
        set->tail->next = entry;

    set->tail = entry;
    set->size += 1;
}

const char *string_list_get_by_index(StringList *set, size_t index) {
    if (index >= set->size) return NULL;

    size_t i = 0;
    for (StringListEntry *cur = set->head; cur; cur = cur->next) {
        if (i++ == index) return cur->key;
    }
    return NULL;
}

void string_list_set_by_index(StringList *list, size_t index, const char *new_value) {
    if (!list || index >= list->size || !new_value) return;

    size_t i = 0;
    for (StringListEntry *cur = list->head; cur; cur = cur->next) {
        if (i++ == index) {
            free(cur->key);  // Free the old value
            cur->key = strdup(new_value);  // Replace with new value
            return;
        }
    }
}

