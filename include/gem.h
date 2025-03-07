// gem.h - Generic Equipment Model (GEM) Header
#ifndef GEM_H
#define GEM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Equipment states
typedef enum {
    GEM_OFFLINE,
    GEM_ONLINE_LOCAL,
    GEM_ONLINE_REMOTE,
    GEM_MAINTENANCE,
    GEM_ERROR
} gem_state_t;

// GEM Object Types
typedef enum {
    GEM_EQUIPMENT,
    GEM_SUBSTRATE,
    GEM_CASSETTE,
    GEM_PROCESS_CHAMBER,
    GEM_CONVEYOR
} gem_object_type_t;

// Generic GEM Object
typedef struct gem_object {
    char id[64];
    gem_object_type_t type;
    struct gem_object *next;
} gem_object_t;

// Equipment Structure
typedef struct {
    char name[128];
    gem_state_t state;
    gem_object_t *objects;
} gem_equipment_t;

// Function Prototypes
void gem_set_state(gem_equipment_t *eq, gem_state_t state);
gem_state_t gem_get_state(gem_equipment_t *eq);
void gem_add_object(gem_equipment_t *eq, gem_object_t *obj);
gem_object_t *gem_query_objects(gem_equipment_t *eq);
void gem_run_command(gem_equipment_t *eq, const char *cmd, const char *args);
void gem_print_equipment_status(gem_equipment_t *eq);

#endif // GEM_H
