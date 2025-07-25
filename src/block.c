#include "block.h"
#include "block_util.h"
#include "instance.h"
#include "log.h"
#include "mkrand.h"
#include "string_list.h"
#include <stdlib.h>
#include <string.h>

void print_block(const Block *blk)
{
    LOG_INFO("Block psi: ");
    print_psi(&blk->psi);
    LOG_INFO("\n");

    for (InstanceList *instance_list = blk->instances; instance_list; instance_list = instance_list->next)
    {
        Instance *instance = instance_list->instance;
        LOG_INFO("  Instance: %s\n", instance->name);

        LOG_INFO("    Definition: %s\n", instance->definition->name);
        LOG_INFO("    Inputs: ");
        for (size_t i = 0; i < string_list_count(instance->definition->input_signals); i++)
        {
            LOG_INFO("%s ", string_list_get_by_index(instance->definition->input_signals, i));
        }

        LOG_INFO("\n    Outputs: ");
        if (instance->definition->output_signals)
        {
            for (size_t i = 0; i < string_list_count(instance->definition->output_signals); i++)
            {
                LOG_INFO("%s ", string_list_get_by_index(instance->definition->output_signals, i));
            }
        }

        LOG_INFO("\n");

        LOG_INFO("    Invocation Target: %s\n", instance->invocation->target_name);
        LOG_INFO("    To: ");
        print_psi(&instance->invocation->to); // ✅ pointer use

        LOG_INFO("\n    Inputs: ");
        for (size_t i = 0; i < string_list_count(instance->invocation->input_signals); i++)
        {
            LOG_INFO("%s ", string_list_get_by_index(instance->invocation->input_signals, i));
        }

        LOG_INFO("\n    Outputs: ");
        for (size_t i = 0; i < string_list_count(instance->invocation->output_signals); i++)
        {
            LOG_INFO("%s ", string_list_get_by_index(instance->invocation->output_signals, i));
        }

        LOG_INFO("\n");
    }
}

void block_add_instance(Block *blk, Instance *instance)
{
    if (!blk || !instance)
        return;

    InstanceList *node = malloc(sizeof(InstanceList));
    if (!node)
    {
        LOG_ERROR("❌ Failed to allocate memory for InstanceList node.");
        return;
    }

    node->instance = instance;
    node->next = NULL;

    if (!blk->instances)
    {
        blk->instances = node;
        return;
    }

    // Walk to end of list
    InstanceList *cur = blk->instances;
    while (cur->next)
        cur = cur->next;

    cur->next = node;
}
