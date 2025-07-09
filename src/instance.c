#include "instance.h"
#include "invocation.h"
#include "eval_util.h"
#include "rewrite_util.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void qualify_invocation_signals(Invocation *inv, const char *instance_name)
{
    if (!inv || !instance_name)
        return;

    // Inputs
    for (size_t i = 0; i < string_list_count(inv->input_signals); ++i)
    {
        const char *old = string_list_get_by_index(inv->input_signals, i);
        if (!old)
            continue;

        if (strchr(old, '.'))
            continue;

        char qualified[256];
        snprintf(qualified, sizeof(qualified), "%s.%s", instance_name, old);
        string_list_set_by_index(inv->input_signals, i, strdup(qualified));
    }

    // Outputs
    for (size_t i = 0; i < string_list_count(inv->output_signals); ++i)
    {
        const char *old = string_list_get_by_index(inv->output_signals, i);
        if (!old)
            continue;

        if (strchr(old, '.'))
            continue;

        char qualified[256];
        snprintf(qualified, sizeof(qualified), "%s.%s", instance_name, old);
        string_list_set_by_index(inv->output_signals, i, strdup(qualified));
    }

    // Literal bindings
    if (inv->literal_bindings)
    {
        for (size_t i = 0; i < inv->literal_bindings->count; ++i)
        {
            LiteralBinding *bind = &inv->literal_bindings->items[i];
            if (!bind || !bind->name)
                continue;

            if (strchr(bind->name, '.'))
                continue;

            char qualified[256];
            snprintf(qualified, sizeof(qualified), "%s.%s", instance_name, bind->name);

            free(bind->name);
            bind->name = strdup(qualified);
        }
    }
}

/**
 * Qualify all signal names in the given Definition with a unique prefix.
 *
 * - Inputs and outputs are prefixed with the instance name if not already qualified.
 * This ensures that all signals are globally unique and correctly mapped to external inputs.
 */
void qualify_definition_signals(Definition *def, const char *instance_name)
{
    if (!def || !instance_name)
        return;

    // Inputs
    for (size_t i = 0; i < string_list_count(def->input_signals); ++i)
    {
        const char *name = string_list_get_by_index(def->input_signals, i);
        if (!name)
            continue;

        if (strchr(name, '.'))
        {
            LOG_INFO("✅ Input signal already qualified, left unchanged: %s", name);
            continue;
        }

        char buf[256];
        snprintf(buf, sizeof(buf), "%s.%s", instance_name, name);
        string_list_set_by_index(def->input_signals, i, strdup(buf));
        LOG_INFO("🔁 Definition input signal qualified: %s → %s", name, buf);
    }

    // Outputs
    for (size_t i = 0; i < string_list_count(def->output_signals); ++i)
    {
        const char *name = string_list_get_by_index(def->output_signals, i);
        if (!name)
            continue;

        if (strchr(name, '.'))
        {
            LOG_INFO("✅ Output signal already qualified, left unchanged: %s", name);
            continue;
        }

        char buf[256];
        snprintf(buf, sizeof(buf), "%s.%s", instance_name, name);
        string_list_set_by_index(def->output_signals, i, strdup(buf));
        LOG_INFO("🔁 Definition output signal qualified: %s → %s", name, buf);
    }
}

void remap_definition_inputs_to_invocation(Definition *def, Invocation *inv)
{
    if (!def || !inv)
        return;

    size_t n_inputs = string_list_count(def->input_signals);

    // Save old input signals to compare
    StringList *old_inputs = string_list_clone(def->input_signals);

    for (size_t i = 0; i < n_inputs; ++i)
    {
        const char *qualified_inv_signal = string_list_get_by_index(inv->input_signals, i);
        if (qualified_inv_signal)
        {
            string_list_set_by_index(def->input_signals, i, strdup(qualified_inv_signal));
            LOG_INFO("🔁 Remapped definition input[%zu]: → %s", i, qualified_inv_signal);
        }
        else
        {
            LOG_WARN("⚠️ Invocation input[%zu] missing while remapping definition input", i);
        }
    }

    // Also remap ConditionalInvocation pattern args
    if (def->conditional_invocation && def->conditional_invocation->pattern_args)
    {
        for (size_t i = 0; i < string_list_count(def->conditional_invocation->pattern_args); ++i)
        {
            const char *arg = string_list_get_by_index(def->conditional_invocation->pattern_args, i);
            if (!arg)
                continue;

            bool remapped = false;
            // Compare against old input names
            for (size_t j = 0; j < n_inputs; ++j)
            {
                const char *old_input = string_list_get_by_index(old_inputs, j);
                if (old_input && strcmp(arg, old_input) == 0)
                {
                    const char *new_signal = string_list_get_by_index(inv->input_signals, j);
                    if (new_signal)
                    {
                        string_list_set_by_index(def->conditional_invocation->pattern_args, i, strdup(new_signal));
                        LOG_INFO("🔁 CI pattern arg remapped: %s → %s", arg, new_signal);
                        remapped = true;
                        break;
                    }
                }
            }

            if (!remapped)
            {
                LOG_INFO("✅ CI pattern arg left unchanged: %s", arg);
            }
        }
    }

    destroy_string_list(old_inputs);
}

Instance *create_instance(const char *def_name, int instance_id, Definition *def, Invocation *inv, const char *parent_prefix, SignalMap *signal_map)
{
    Instance *instance = malloc(sizeof(Instance));
    if (!instance)
        return NULL;

    // Compose fully qualified name
    char buf[256];
    if (parent_prefix)
    {
        snprintf(buf, sizeof(buf), "%s.%s.%d", parent_prefix, def_name, instance_id);
    }
    else
    {
        snprintf(buf, sizeof(buf), "INV.%s.%d", def_name, instance_id);
    }
    instance->name = strdup(buf);

    // Clone definition and invocation
    instance->invocation = clone_invocation(inv);
    // Rewrite invocation signals to be fully qualified
    qualify_invocation_signals(instance->invocation, instance->name);

    instance->definition = clone_definition(def);
    remap_definition_inputs_to_invocation(instance->definition, instance->invocation);

    // Rewrite definition signals to be fully qualified
    qualify_definition_signals(instance->definition, instance->name);

    dump_literal_bindings(instance->invocation);

    return instance;
}

void destroy_instance(Instance *inst)
{
    if (!inst)
        return;

    // Free Invocation
    if (inst->invocation)
    {
        if (inst->invocation->input_signals)
            destroy_string_list(inst->invocation->input_signals);
        if (inst->invocation->output_signals)
            destroy_string_list(inst->invocation->output_signals);
        if (inst->invocation->literal_bindings)
        {
            free(inst->invocation->literal_bindings->items);
            free(inst->invocation->literal_bindings);
        }
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
            destroy_string_list(ci->pattern_args);
            free(ci->output);
            free(ci->cases);
            free(ci);
        }

        // Free Body items
        BodyItem *item = inst->definition->body;
        while (item)
        {
            BodyItem *next_item = item->next;
            if (item->type == BODY_SIGNAL_INPUT || item->type == BODY_SIGNAL_OUTPUT)
                free(item->data.signal_name);
            // Note: BODY_INVOCATION embedded instances are expanded elsewhere, no free here.
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

void free_instance_list(InstanceList *list)
{
    while (list)
    {
        InstanceList *next = list->next;
        if (list->instance)
            destroy_instance(list->instance);
        free(list);
        list = next;
    }
}
