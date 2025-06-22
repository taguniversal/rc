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


Definition *find_definition_by_name(Block *blk, const char *name)
{
    if (!blk || !name)
        return NULL;

    for (Definition *def = blk->definitions; def != NULL; def = def->next)
    {
        if (def->name && strcmp(def->name, name) == 0)
        {
            return def;
        }
    }

    return NULL; // Not found
}

void qualify_signal_names(Invocation *inv)
{
    if (!inv || !inv->target_name)
        return;

    char prefix[128];
    snprintf(prefix, sizeof(prefix), "INV.%s.%d", inv->target_name, inv->instance_id);

    // Helper to prepend the prefix to each signal name
    for (size_t i = 0; i < inv->input_signals->size; ++i)
    {
        const char *old = string_list_get_by_index(inv->input_signals, i);
        char qualified[256];
        snprintf(qualified, sizeof(qualified), "%s.%s", prefix, old);
        string_list_set_by_index(inv->input_signals, i, strdup(qualified));
    }

    for (size_t i = 0; i < inv->output_signals->size; ++i)
    {
        const char *old = string_list_get_by_index(inv->output_signals, i);
        char qualified[256];
        snprintf(qualified, sizeof(qualified), "%s.%s", prefix, old);
        string_list_set_by_index(inv->output_signals, i, strdup(qualified));
    }

    // Optional: qualify bindings
    if (inv->literal_bindings)
    {
        for (size_t i = 0; i < inv->literal_bindings->count; ++i)
        {
            LiteralBinding *bind = &inv->literal_bindings->items[i];
            if (bind->name)
            {
                char qualified[256];
                snprintf(qualified, sizeof(qualified), "%s.%s", prefix, bind->name);
                free(bind->name);
                bind->name = strdup(qualified);
            }
        }
    }
}

void qualify_signal_names_with_prefix(Invocation *inv, const char *prefix)
{
    if (!inv || !prefix) return;

    for (size_t i = 0; i < string_list_count(inv->input_signals); ++i)
    {
        char *old = string_list_get_by_index(inv->input_signals, i);
        char qualified[256];
        snprintf(qualified, sizeof(qualified), "%s.%s", prefix, old);
        string_list_set_by_index(inv->input_signals, i, strdup(qualified));
        free(old);
    }

    for (size_t i = 0; i < string_list_count(inv->output_signals); ++i)
    {
        char *old = string_list_get_by_index(inv->output_signals, i);
        char qualified[256];
        snprintf(qualified, sizeof(qualified), "%s.%s", prefix, old);
        string_list_set_by_index(inv->output_signals, i, strdup(qualified));
        free(old);
    }
}


int expand_embedded_invocations(Block *blk, Instance *parent_instance)
{
    if (!parent_instance || !parent_instance->definition)
        return 0;

    BodyItem *body = parent_instance->definition->body;
    size_t sub_index = 0;
    int added = 0;

    while (body)
    {
        if (body->type == BODY_INVOCATION)
        {
            Invocation *sub_inv = body->data.invocation;

            char nested_name[256];
            snprintf(nested_name, sizeof(nested_name), "%s.%s.%zu", parent_instance->name, sub_inv->target_name, sub_index);

            sub_inv->instance_id = sub_index;
           
            Definition *sub_def = find_definition_by_name(blk, sub_inv->target_name);
            if (!sub_def)
            {
                LOG_WARN("⚠️ No definition for nested invocation %s", sub_inv->target_name);
                body = body->next;
                continue;
            }

            Instance *sub_instance = create_instance(sub_inv->target_name, sub_index, sub_def, sub_inv);
            if (!sub_instance)
            {
                LOG_ERROR("❌ Failed to create nested instance: %s", nested_name);
                body = body->next;
                continue;
            }

            sub_instance->name = strdup(nested_name);
            qualify_signal_names_with_prefix(sub_inv, sub_instance->name);

            block_add_instance(blk, sub_instance);
            LOG_INFO("🧩 Created embedded instance: %s (target=%s)", sub_instance->name, sub_inv->target_name);
            added++;

            // Recursively expand embedded invocations inside this instance too
            added += expand_embedded_invocations(blk, sub_instance);
        }

        body = body->next;
        sub_index++;
    }

    return added;
}

void unify_invocations(Block *blk)
{
    LOG_INFO("🔗 Building Instances...");

    if (!blk) return;

    size_t count = 0;
    size_t embedded_count = 0;

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
        block_add_instance(blk, instance);
        count++;

        // 🔍 Expand embedded invocations inside the definition of this instance
        LOG_INFO("🔍 Checking for embedded invocations in: %s", instance->name);
        embedded_count += expand_embedded_invocations(blk, instance);
    }

    LOG_INFO("✅ Built %zu instance(s)", count + embedded_count);
}

