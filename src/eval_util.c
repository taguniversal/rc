#include "eval.h"
#include "log.h"
#include "eval_util.h"
#include "block_util.c"
#include "invocation.h"
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>




Instance *make_instance(const char *name, Definition *def, Invocation *inv)
{
    Instance *instance = malloc(sizeof(Instance));
    if (!instance)
        return NULL;

    instance->definition = def;
    instance->invocation = inv;

    return instance;
}

void append_instance(Block *blk, Instance *instance)
{
    if (!blk || !instance)
        return;

    instance->next = NULL;

    if (!blk->instances)
    {
        blk->instances = instance;
    }
    else
    {
        Instance *current = blk->instances;
        while (current->next)
        {
            current = current->next;
        }
        current->next = instance;
    }
}

static char *prepend_unit_name(const char *unit_name, const char *sig)
{
    if (!sig)
        return NULL;
    size_t len = strlen(unit_name) + 1 + strlen(sig) + 1;
    char *buf = malloc(len);
    snprintf(buf, len, "%s.%s", unit_name, sig);
    return buf;
}


void globalize_signal_names(Block *blk)
{
    LOG_INFO("\U0001F30D Starting signal globalization for all Instances...");

    for (Instance *inst = blk->instances; inst != NULL; inst = inst->next)
    {
        const char *instance_name = inst->invocation->target_name;

        // --- Invocation inputs and outputs ---
        for (size_t i = 0; i < string_list_count(inst->invocation->input_signals); ++i)
        {
            const char *name = string_list_get_by_index(inst->invocation->input_signals, i);
            if (name)
            {
                char *updated = prepend_unit_name(instance_name, name);
                inst->invocation->input_signals->items[i] = updated;
            }
        }

        for (size_t i = 0; i < string_list_count(inst->invocation->output_signals); ++i)
        {
            const char *name = string_list_get_by_index(inst->invocation->output_signals, i);
            if (name)
            {
                char *updated = prepend_unit_name(instance_name, name);
                inst->invocation->output_signals->items[i] = updated;
            }
        }

        // --- Definition inputs and outputs ---
        for (size_t i = 0; i < string_list_count(inst->definition->input_signals); ++i)
        {
            const char *name = string_list_get_by_index(inst->definition->input_signals, i);
            if (name)
            {
                char *updated = prepend_unit_name(instance_name, name);
                inst->definition->input_signals->items[i] = updated;
            }
        }

        for (size_t i = 0; i < string_list_count(inst->definition->output_signals); ++i)
        {
            const char *name = string_list_get_by_index(inst->definition->output_signals, i);
            if (name)
            {
                char *updated = prepend_unit_name(instance_name, name);
                inst->definition->output_signals->items[i] = updated;
            }
        }

        // --- ConditionalInvocation (if present) ---
        ConditionalInvocation *ci = inst->definition->conditional_invocation;
        if (ci)
        {
            if (ci->pattern)
            {
                char *updated_pattern = prepend_unit_name(instance_name, ci->pattern);
                free(ci->pattern);
                ci->pattern = updated_pattern;
            }
            if (ci->output)
            {
                char *updated_output = prepend_unit_name(instance_name, ci->output);
                free(ci->output);
                ci->output = updated_output;
            }
        }

        LOG_INFO("✅ Signals globalized for instance: %s", instance_name);
    }

    LOG_INFO("\U0001F30D Signal globalization complete.");
}


static Unit *create_unit(const char *def_name, int instance_id, Definition *def, Invocation *inv)
{
    Unit *unit = malloc(sizeof(Unit));
    if (!unit)
        return NULL;

    // Construct unit name: INV.<Definition>.<Instance>
    char buf[256];
    snprintf(buf, sizeof(buf), "INV.%s.%d", def_name, instance_id);
    unit->name = strdup(buf);
    unit->definition = def;
    unit->invocation = inv;

    return unit;
}

Invocation *clone_invocation(const Invocation *src, const char *unit_prefix)
{
    if (!src || !unit_prefix)
        return NULL;

    Invocation *inv = calloc(1, sizeof(Invocation));
    if (!inv)
        return NULL;

    inv->name = strdup(src->name);
    if (!inv->name)
    {
        free(inv);
        return NULL;
    }

    inv->origin_sexpr_path = src->origin_sexpr_path ? strdup(src->origin_sexpr_path) : NULL;

    inv->next = NULL;

    // --- Clone boundary_sources ---
    inv->boundary_sources.count = src->boundary_sources.count;
    inv->boundary_sources.items = calloc(inv->boundary_sources.count, sizeof(SourcePlace *));
    if (!inv->boundary_sources.items && inv->boundary_sources.count > 0)
        goto fail;

    for (size_t i = 0; i < inv->boundary_sources.count; ++i)
    {
        SourcePlace *orig = src->boundary_sources.items[i];
        SourcePlace *copy = calloc(1, sizeof(SourcePlace));
        if (!copy)
            goto fail;

        // 🔁 Explicit deep copy
        copy->name = orig->name ? strdup(orig->name) : NULL;
        copy->resolved_name = orig->resolved_name ? strdup(orig->resolved_name) : NULL;
        copy->content = orig->content ? strdup(orig->content) : NULL;

        // Copy primitive fields here as needed
        // copy->index = orig->index;

        inv->boundary_sources.items[i] = copy;
    }

    // --- Clone boundary_destinations ---
    inv->boundary_destinations.count = src->boundary_destinations.count;
    inv->boundary_destinations.items = calloc(inv->boundary_destinations.count, sizeof(DestinationPlace *));
    if (!inv->boundary_destinations.items && inv->boundary_destinations.count > 0)
        goto fail;

    for (size_t i = 0; i < inv->boundary_destinations.count; ++i)
    {
        DestinationPlace *orig = src->boundary_destinations.items[i];
        DestinationPlace *copy = calloc(1, sizeof(DestinationPlace));
        if (!copy)
            goto fail;

        // 🔁 Explicit deep copy of all fields
        copy->name = orig->name ? strdup(orig->name) : NULL;
        copy->resolved_name = orig->resolved_name ? strdup(orig->resolved_name) : NULL;
        copy->content = orig->content ? strdup(orig->content) : NULL;

        // Copy primitive fields here, if any (e.g. copy->index = orig->index)

        inv->boundary_destinations.items[i] = copy;
    }

    return inv;

fail:
    if (inv)
    {
        free(inv->name);

        for (size_t i = 0; i < inv->boundary_sources.count; ++i)
            free(inv->boundary_sources.items[i]);
        free(inv->boundary_sources.items);

        for (size_t i = 0; i < inv->boundary_destinations.count; ++i)
            free(inv->boundary_destinations.items[i]);
        free(inv->boundary_destinations.items);

        free(inv);
    }

    return NULL;
}

Definition *clone_definition(const Definition *src, const char *unit_prefix)
{
    if (!src || !unit_prefix)
        return NULL;

    Definition *def = calloc(1, sizeof(Definition));
    if (!def)
        return NULL;

    def->name = strdup(src->name);
    if (!def->name)
    {
        free(def);
        return NULL;
    }

    def->origin_sexpr_path = src->origin_sexpr_path ? strdup(src->origin_sexpr_path) : NULL;

    def->next = NULL;

    // --- Clone boundary_sources ---
    def->boundary_sources.count = src->boundary_sources.count;
    def->boundary_sources.items = calloc(def->boundary_sources.count, sizeof(SourcePlace *));
    if (!def->boundary_sources.items && def->boundary_sources.count > 0)
        goto fail;

    for (size_t i = 0; i < def->boundary_sources.count; ++i)
    {
        SourcePlace *orig = src->boundary_sources.items[i];
        SourcePlace *copy = calloc(1, sizeof(SourcePlace));
        copy->name = orig->name ? strdup(orig->name) : NULL;
        copy->resolved_name = orig->resolved_name ? strdup(orig->resolved_name) : NULL;
        copy->content = orig->content ? strdup(orig->content) : NULL;
        // Copy other primitive fields here if needed (e.g. int index, enum flags, etc.)

        def->boundary_sources.items[i] = copy;
    }

    // --- Clone boundary_destinations ---
    def->boundary_destinations.count = src->boundary_destinations.count;
    def->boundary_destinations.items = calloc(def->boundary_destinations.count, sizeof(DestinationPlace *));

    if (!def->boundary_destinations.items && def->boundary_destinations.count > 0)
        goto fail;

    for (size_t i = 0; i < def->boundary_destinations.count; ++i)
    {
        DestinationPlace *orig = src->boundary_destinations.items[i];
        DestinationPlace *copy = calloc(1, sizeof(DestinationPlace));
        if (!copy)
            goto fail;

        // 🔁 Deep copy all fields explicitly
        copy->name = orig->name ? strdup(orig->name) : NULL;
        copy->resolved_name = orig->resolved_name ? strdup(orig->resolved_name) : NULL;
        copy->content = orig->content ? strdup(orig->content) : NULL;

        // If you have any other primitive fields (e.g. flags, enum type, etc.), copy them directly here:
        // copy->some_flag = orig->some_flag;

        def->boundary_destinations.items[i] = copy;
    }

    // --- Clone POR sources ---
    def->place_of_resolution_sources.count = src->place_of_resolution_sources.count;
    def->place_of_resolution_sources.items = calloc(def->place_of_resolution_sources.count, sizeof(SourcePlace *));
    if (!def->place_of_resolution_sources.items && def->place_of_resolution_sources.count > 0)
        goto fail;

    for (size_t i = 0; i < def->place_of_resolution_sources.count; ++i)
    {
        SourcePlace *orig = src->place_of_resolution_sources.items[i];
        SourcePlace *copy = calloc(1, sizeof(SourcePlace));
        if (!copy)
            goto fail;

        // 🔁 Deep copy all relevant fields explicitly
        copy->name = orig->name ? strdup(orig->name) : NULL;
        copy->resolved_name = orig->resolved_name ? strdup(orig->resolved_name) : NULL;
        copy->content = orig->content ? strdup(orig->content) : NULL;

        // Copy any other non-pointer fields here if present:
        // copy->role = orig->role;  // example

        def->place_of_resolution_sources.items[i] = copy;
    }

    // --- Clone POR invocations ---
    def->place_of_resolution_invocations = NULL;
    Invocation **tail = &def->place_of_resolution_invocations;

    for (Invocation *cur = src->place_of_resolution_invocations; cur; cur = cur->next)
    {
        Invocation *copy = clone_invocation(cur, unit_prefix);
        if (!copy)
            goto fail;

        *tail = copy;
        tail = &copy->next;
    }

    return def;

fail:
    // Clean up partial allocation (not full free_tree, just enough to not leak)
    if (def)
    {
        free(def->name);

        for (size_t i = 0; i < def->boundary_sources.count; ++i)
            free(def->boundary_sources.items[i]);
        free(def->boundary_sources.items);

        for (size_t i = 0; i < def->boundary_destinations.count; ++i)
            free(def->boundary_destinations.items[i]);
        free(def->boundary_destinations.items);

        for (size_t i = 0; i < def->place_of_resolution_sources.count; ++i)
            free(def->place_of_resolution_sources.items[i]);
        free(def->place_of_resolution_sources.items);

        Invocation *inv = def->place_of_resolution_invocations;
        while (inv)
        {
            Invocation *next = inv->next;
            free(inv); // optionally call a deeper free here
            inv = next;
        }

        free(def);
    }

    return NULL;
}

void unify_invocations(Block *blk)
{
    LOG_INFO("🏗  Building Unit structures (invocation/definition pairs)...");

    UnitList *head = NULL;
    UnitList **tail = &head;

    size_t count = 0;

    for (Invocation *inv = blk->invocations; inv != NULL; inv = inv->next)
    {
        LOG_INFO("📦 Visiting Invocation: %s (%p)", inv->name, (void *)inv);
    }

    for (Invocation *inv = blk->invocations; inv != NULL; inv = inv->next)
    {
        // Find matching definition
        Definition *def = blk->definitions;
        while (def && strcmp(def->name, inv->name) != 0)
        {
            def = def->next;
        }

        if (!def)
        {
            LOG_WARN("⚠️  No matching definition for invocation: %s", inv->name);
            continue;
        }

        // ✅ Declare and initialize buf BEFORE it's used
        char buf[256];
        snprintf(buf, sizeof(buf), "INV.%s.%d", inv->name, inv->instance_id);

        LOG_INFO("🔗 Creating Unit: %s", buf);

        Definition *def_copy = clone_definition(def, buf);
        Invocation *inv_copy = clone_invocation(inv, buf);

        // Construct Unit
        Unit *unit = malloc(sizeof(Unit));
        unit->name = strdup(buf);
        unit->definition = def_copy;
        unit->invocation = inv_copy;

        // Append to UnitList
        UnitList *node = malloc(sizeof(UnitList));
        node->unit = unit;
        node->next = NULL;

        *tail = node;
        tail = &node->next;
        count += 1;
    }

    blk->units = head;
    LOG_INFO("✅ Finished building %zu unit(s)", count);
}
