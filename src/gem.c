// gem.c - Generic Equipment Model (GEM) Implementation
#include "gem.h"

void gem_set_state(gem_equipment_t *eq, gem_state_t state) {
    eq->state = state;
}

gem_state_t gem_get_state(gem_equipment_t *eq) {
    return eq->state;
}

void gem_add_object(gem_equipment_t *eq, gem_object_t *obj) {
    obj->next = eq->objects;
    eq->objects = obj;
}

gem_object_t *gem_query_objects(gem_equipment_t *eq) {
    return eq->objects;
}

void gem_run_command(gem_equipment_t *eq, const char *cmd, const char *args) {
    printf("[GEM] Running command on %s: %s %s\n", eq->name, cmd, args ? args : "");
}

void gem_print_equipment_status(gem_equipment_t *eq) {
    printf("[GEM] Equipment: %s\n", eq->name);
    printf("[GEM] State: %d\n", eq->state);
    printf("[GEM] Objects:\n");
    gem_object_t *obj = eq->objects;
    while (obj) {
        printf("  - %s (Type: %d)\n", obj->id, obj->type);
        obj = obj->next;
    }
}
