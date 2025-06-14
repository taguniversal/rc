#include "instance.h"
#include "invocation.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

Instance *create_instance(const char *def_name, int instance_id, Definition *def, Invocation *inv)
{
    Instance *instance = malloc(sizeof(Instance));
    if (!instance)
        return NULL;

    // Construct unit name: INV.<Definition>.<Instance>
    char buf[256];
    snprintf(buf, sizeof(buf), "INV.%s.%d", def_name, instance_id);
    instance->name = strdup(buf);
    instance->definition = def;
    instance->invocation = inv;

    return instance;
}

void free_instance_list(InstanceList *list)
{
    while (list)
    {
        InstanceList *next = list->next;
        Instance *inst = list->instance;

        if (inst)
        {
            // Free Invocation
            if (inst->invocation)
            {
                if (inst->invocation->input_signals)
                    destroy_string_list(inst->invocation->input_signals);
                if (inst->invocation->output_signals)
                    destroy_string_list(inst->invocation->output_signals);
                if (inst->invocation->literal_bindings)
                    free(inst->invocation->literal_bindings->items);
                free(inst->invocation->literal_bindings);
                free(inst->invocation->target_name);
                free(inst->invocation);
            }

            // Free Definition
            if (inst->definition)
            {
                if (inst->definition->input_signals)
                    destroy_string_list(inst->definition->input_signals);
                if (inst->definition->output_signals)
                    destroy_string_list(inst->definition->output_signals);

                // Free ConditionalInvocation
                if (inst->definition->conditional_invocation)
                {
                    ConditionalInvocation *ci = inst->definition->conditional_invocation;

                    destroy_string_list(ci->pattern_args); // ← clean all pattern args properly
                    free(ci->output);                      // if allocated with strdup
                    free(ci->cases);                       // if malloc'ed as array of ConditionalCase
                    free(ci);
                }

                // Free Body items
                BodyItem *item = inst->definition->body;
                while (item)
                {
                    BodyItem *next_item = item->next;
                    if (item->type == BODY_SIGNAL_INPUT || item->type == BODY_SIGNAL_OUTPUT)
                        free(item->data.signal_name);
                    else if (item->type == BODY_INVOCATION)
                        ; // Already freed above as part of inst->invocation
                    free(item);
                    item = next_item;
                }

                free(inst->definition->name);
                free(inst->definition->origin_sexpr_path);
                free(inst->definition->sexpr_logic);
                free(inst->definition);
            }

            free(inst->name);
            free(inst);
        }

        free(list);
        list = next;
    }
}
