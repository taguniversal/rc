#ifndef INSTANCE_H
#define INSTANCE_H
#include "invocation.h"
#include "signal_map.h"

typedef struct Instance {
   char *name;
   Definition *definition;
   Invocation *invocation;
} Instance;

typedef struct InstanceList {
    Instance *instance;
    struct InstanceList *next;
}  InstanceList;


Instance *create_instance(const char *def_name, int instance_id, Definition *def, Invocation *inv, const char *parent_prefix, SignalMap* signal_map);

void destroy_instance(Instance *inst);

#endif
