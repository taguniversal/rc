#ifndef INSTANCE_H
#define INSTANCE_H


typedef struct Instance {
   char *name;
   Definition *definition;
   Invocation *invocation;
} Instance;

typedef struct InstanceList {
    Instance *instance;
    struct InstanceList *next;
}  InstanceList;


Instance *create_instance(const char *def_name, int instance_id, Definition *def, Invocation *inv);


#endif
