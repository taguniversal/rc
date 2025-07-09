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



int expand_embedded_invocations(Block *blk, Instance *parent_instance, SignalMap *signal_map)
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

            sub_inv->instance_id = sub_index;

            Definition *sub_def = find_definition_by_name(blk, sub_inv->target_name);
            if (!sub_def)
            {
                LOG_WARN("⚠️ No definition for nested invocation %s", sub_inv->target_name);
                body = body->next;
                continue;
            }

            // 👍 Correct create_instance call
            Instance *sub_instance = create_instance(
                sub_inv->target_name,   // def_name
                sub_index,              // instance_id
                sub_def,                // definition
                sub_inv,                // invocation
                parent_instance->name , // parent prefix
                signal_map
            );

            if (!sub_instance)
            {
                LOG_ERROR("❌ Failed to create nested instance: %s.%s.%zu",
                          parent_instance->name, sub_inv->target_name, sub_index);
                body = body->next;
                continue;
            }

            // All input rewrites are no longer needed here if qualify_* handled them
            block_add_instance(blk, sub_instance);
            LOG_INFO("🧩 Created embedded instance: %s (target=%s)", sub_instance->name, sub_inv->target_name);

            // Recurse
            added += 1 + expand_embedded_invocations(blk, sub_instance, signal_map);
        }

        body = body->next;
        sub_index++;
    }

    return added;
}



void publish_all_literal_bindings(Block *blk, SignalMap *signal_map)
{
    if (!blk || !blk->instances)
        return;

    LOG_INFO("🪄 Publishing all literal bindings to signal map...");

    for (InstanceList *node = blk->instances; node; node = node->next)
    {
        Instance *inst = node->instance;
        if (!inst || !inst->invocation || !inst->invocation->literal_bindings)
            continue;

        for (size_t i = 0; i < inst->invocation->literal_bindings->count; ++i)
        {
            LiteralBinding *b = &inst->invocation->literal_bindings->items[i];
            if (b->name && b->value)
            {
                update_signal_value(signal_map, b->name, b->value);
                LOG_INFO("📥 Published literal binding: %s = %s", b->name, b->value);
            }
        }
    }

    LOG_INFO("✅ All literal bindings published.");
}

void unify_invocations(Block *blk, SignalMap *signal_map)
{
    LOG_INFO("🔗 Building Instances...");

    if (!blk)
        return;

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

        // Create container instance (no parent prefix)
        Instance *instance = create_instance(
            inv->target_name,     // def_name
            inv->instance_id,     // instance_id
            def,                  // definition
            inv,                  // invocation
            NULL ,                // parent_prefix
            signal_map
        );

        if (!instance)
        {
            LOG_ERROR("❌ Failed to create instance for %s.%d", inv->target_name, inv->instance_id);
            continue;
        }

        block_add_instance(blk, instance);
        count++;

        size_t added = expand_embedded_invocations(blk, instance, signal_map);
        embedded_count += added;
    }

    LOG_INFO("✅ Built %zu instance(s)", count + embedded_count);
}


void dump_literal_bindings(Invocation *inv)
{
    if (!inv)
    {
        LOG_INFO("ℹ️ No invocation provided to dump.");
        return;
    }

    if (!inv->literal_bindings || inv->literal_bindings->count == 0)
    {
        LOG_INFO("ℹ️ Invocation '%s' has no literal bindings.", inv->target_name ? inv->target_name : "(unknown)");
        return;
    }

    LOG_INFO("🔎 Literal Bindings for Invocation '%s' (count=%zu):",
             inv->target_name ? inv->target_name : "(unknown)",
             inv->literal_bindings->count);

    for (size_t i = 0; i < inv->literal_bindings->count; ++i)
    {
        LiteralBinding *b = &inv->literal_bindings->items[i];
        if (!b)
        {
            LOG_WARN("⚠️ Null binding at index %zu", i);
            continue;
        }
        LOG_INFO("   • [%zu] %s = %s",
                 i,
                 b->name ? b->name : "(null)",
                 b->value ? b->value : "(null)");
    }
}
