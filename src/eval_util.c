#include "eval_util.h"
#include "eval.h"
#include "log.h"
#include "block.h"
#include "instance.h"
#include "invocation.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// 🌍 Helper: Prepend instance name to signal
static char *prepend_instance_name(const char *instanec_name, const char *sig)
{
    if (!sig)
        return NULL;
    size_t len = strlen(instanec_name) + strlen(sig) + 2;
    char *buf = malloc(len);
    snprintf(buf, len, "%s.%s", instanec_name, sig);
    return buf;
}

void globalize_signal_names(Block *blk)
{
    LOG_INFO("🌍 Globalizing signal names...");

    for (InstanceList *node = blk->instances; node; node = node->next)
    {
        Instance *inst = node->instance;
        if (!inst || !inst->invocation || !inst->definition)
            continue;

        const char *prefix = inst->invocation->target_name;

        // Invocation input signals
        for (size_t i = 0; i < string_list_count(inst->invocation->input_signals); i++)
        {
            const char *s = string_list_get_by_index(inst->invocation->input_signals, i);
            char *updated = prepend_instance_name(prefix, s);
            string_list_set_by_index(inst->invocation->input_signals, i, updated);
        }

        // Invocation output signals
        for (size_t i = 0; i < string_list_count(inst->invocation->output_signals); i++)
        {
            const char *s = string_list_get_by_index(inst->invocation->output_signals, i);
            char *updated = prepend_instance_name(prefix, s);
            string_list_set_by_index(inst->invocation->output_signals, i, updated);
        }

        // Definition input signals
        for (size_t i = 0; i < string_list_count(inst->definition->input_signals); i++)
        {
            const char *s = string_list_get_by_index(inst->definition->input_signals, i);
            char *updated = prepend_instance_name(prefix, s);
            string_list_set_by_index(inst->definition->input_signals, i, updated);
        }

        // Definition output signals
        for (size_t i = 0; i < string_list_count(inst->definition->output_signals); i++)
        {
            const char *s = string_list_get_by_index(inst->definition->output_signals, i);
            char *updated = prepend_instance_name(prefix, s);
            string_list_set_by_index(inst->definition->output_signals, i, updated);
        }

        // ConditionalInvocation args and output
        if (inst->definition->conditional_invocation)
        {
            ConditionalInvocation *ci = inst->definition->conditional_invocation;

            for (size_t i = 0; i < string_list_count(ci->pattern_args); i++)
            {
                const char *name = string_list_get_by_index(ci->pattern_args, i);
                if (name)
                {
                    char *updated = prepend_instance_name(prefix, name);
                    string_list_set_by_index(ci->pattern_args, i, updated);
                }
            }

            if (ci->output)
            {
                char *updated = prepend_instance_name(prefix, ci->output);
                free(ci->output);
                ci->output = updated;
            }
        }
    }

    LOG_INFO("🌍 Signal globalization complete.");
}

void unify_invocations(Block *blk)
{
    LOG_INFO("🔗 Building Instances...");

    InstanceList *head = NULL;
    InstanceList **tail = &head;
    size_t count = 0;

    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
        // Find matching definition
        Definition *def = blk->definitions;
        while (def && strcmp(def->name, inv->target_name) != 0)
            def = def->next;

        if (!def)
        {
            LOG_WARN("⚠️ No definition for invocation %s", inv->target_name);
            continue;
        }

        Instance *instance = create_instance(inv->target_name, inv->instance_id, def, inv);
        if (!instance)
        {
            LOG_ERROR("❌ Failed to create instance for %s.%d", inv->target_name, inv->instance_id);
            continue;
        }

        // Create and link instance list node
        InstanceList *node = malloc(sizeof(InstanceList));
        if (!node)
        {
            LOG_ERROR("❌ Failed to allocate InstanceList node");
            destroy_invocation(instance->invocation);
            destroy_definition(instance->definition);
            free(instance->name);
            free(instance);
            continue;
        }

        node->instance = instance;
        node->next = NULL;

        *tail = node;
        tail = &node->next;
        count++;
    }

    blk->instances = head;
    LOG_INFO("✅ Built %zu instance(s)", count);
}


