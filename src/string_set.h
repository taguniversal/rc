#ifndef STRING_SET_H
#define STRING_SET_H

typedef struct StringSetEntry {
    char *key;
    int count;
    struct StringSetEntry *next;
} StringSetEntry;

typedef struct {
    StringSetEntry *head;
} StringSet;

StringSet *create_string_set(void);
void destroy_string_set(StringSet *set);
void string_set_add_or_increment(StringSet *set, const char *key);
int string_set_count(StringSet *set, const char *key);

#endif
