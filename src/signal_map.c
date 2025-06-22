#include "signal_map.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char *strdup_safe(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

SignalMap *create_signal_map(void) {
    SignalMap *map = malloc(sizeof(SignalMap));
    if (!map) return NULL;
    map->head = NULL;
    map->count = 0;
    return map;
}

void destroy_signal_map(SignalMap *map) {
    if (!map) return;
    SignalEntry *entry = map->head;
    while (entry) {
        SignalEntry *next = entry->next;
        free(entry->name);
        free(entry->value);
        free(entry);
        entry = next;
    }
    free(map);
}

void update_signal_value(SignalMap *map, const char *name, const char *value) {
    if (!map || !name || !value) return;

    SignalEntry *entry = map->head;
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            free(entry->value);
            entry->value = strdup_safe(value);
            return;
        }
        entry = entry->next;
    }

    // Not found – create new entry
    SignalEntry *new_entry = malloc(sizeof(SignalEntry));
    if (!new_entry) return;

    new_entry->name = strdup_safe(name);
    new_entry->value = strdup_safe(value);
    new_entry->next = map->head;
    map->head = new_entry;
    map->count++;
}

const char *get_signal_value(SignalMap *map, const char *name) {
    if (!map || !name) return NULL;
    SignalEntry *entry = map->head;
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    return NULL;
}

void print_signal_map(SignalMap *map)
{
    if (!map) {
        LOG_WARN("⚠️ Signal map is NULL");
        return;
    }

    LOG_INFO("📡 Signal Map Contents (%zu signals):", map->count);

    SignalEntry *entry = map->head;
    while (entry)
    {
        const char *val = entry->value ? entry->value : "NULL";
        LOG_INFO("   🔹 %s = %s", entry->name, val);
        entry = entry->next;
    }

    if (!map->head)
    {
        LOG_INFO("   (empty)");
    }
}

