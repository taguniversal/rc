#ifndef SIGNAL_MAP_H
#define SIGNAL_MAP_H

#include <stddef.h>

typedef struct SignalEntry {
    char *name;
    char *value;
    struct SignalEntry *next;
} SignalEntry;

typedef struct {
    SignalEntry *head;
    size_t count;
} SignalMap;

SignalMap *create_signal_map(void);
void destroy_signal_map(SignalMap *map);

void update_signal_value(SignalMap *map, const char *name, const char *value);
const char *get_signal_value(SignalMap *map, const char *name); // NULL if not found
void print_signal_map(SignalMap* map);
#endif
