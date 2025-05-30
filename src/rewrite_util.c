#include "rewrite_util.h"
#include "log.h" // your logging macros
#include "log.h"
#include "eval_util.h"
#include "string_set.h"
#include <stdio.h>  // for asprintf
#include <stdlib.h> // for free
#include <string.h> // for strcmp
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

NameCounter *counters = NULL;

int get_next_instance_id(const char *name)
{
    for (NameCounter *nc = counters; nc; nc = nc->next)
    {
        if (strcmp(nc->name, name) == 0)
        {
            return ++nc->count;
        }
    }
    NameCounter *nc = calloc(1, sizeof(NameCounter));
    nc->name = strdup(name);
    nc->count = 0;
    nc->next = counters;
    counters = nc;
    return 0;
}

void rewrite_top_level_invocations(Block *blk)
{
    for (Invocation *inv = blk->invocations; inv != NULL; inv = inv->next)
    {
        int id = get_next_instance_id(inv->name);
        inv->instance_id = id;

        // 🔁 Rewrite each SourcePlace
        for (size_t i = 0; i < inv->boundary_sources.count; ++i)
        {
            SourcePlace *src = inv->boundary_sources.items[i];
            if (!src || !src->name)
                continue;

            char *new_name;
            asprintf(&new_name, "%s.%d.%s", inv->name, id, src->name);
            src->resolved_name = new_name;
            LOG_INFO("🔁 SourcePlace rewritten: %s → %s", src->name, new_name);
        }

        // 🔁 Rewrite each DestinationPlace
        for (size_t i = 0; i < inv->boundary_destinations.count; ++i)
        {
            DestinationPlace *dst = inv->boundary_destinations.items[i];
            if (!dst)
                continue;

            if (!dst->name)
            {
                char synth[64];
                snprintf(synth, sizeof(synth), "%s.%d.%zu", inv->name, id, i);
                dst->name = strdup(synth);
                LOG_INFO("💡 Synthesized destination name: %s", dst->name);
            }

            char *new_name;
            if (strncmp(dst->name, inv->name, strlen(inv->name)) == 0)
            {
                dst->resolved_name = strdup(dst->name);
                LOG_INFO("✅ Skipping double-prefix: %s", dst->resolved_name);
            }
            else
            {
                asprintf(&new_name, "%s.%d.%s", inv->name, id, dst->name);
                dst->resolved_name = new_name;
                LOG_INFO("🔁 DestinationPlace rewritten: %s → %s", dst->name, new_name);
            }
        }
    }
}

void rewrite_definition_signals(Definition *def)
{
    for (size_t i = 0; i < def->boundary_sources.count; ++i)
    {
        SourcePlace *src = def->boundary_sources.items[i];
        char *new_name;
        asprintf(&new_name, "%s.local.%s", def->name, src->name);
        src->resolved_name = new_name;
        LOG_INFO("🔁 Definition SourcePlace rewritten: %s → %s", src->name, new_name);
    }

    for (size_t i = 0; i < def->boundary_destinations.count; ++i)
    {
        DestinationPlace *dst = def->boundary_destinations.items[i];
        char *new_name;
        asprintf(&new_name, "%s.local.%s", def->name, dst->name);
        dst->resolved_name = new_name;
        LOG_INFO("🔁 Definition DestinationPlace rewritten: %s → %s", dst->name, new_name);
    }

    if (def->conditional_invocation && def->conditional_invocation->output)
    {
        char *new_output;
        asprintf(&new_output, "%s.local.%s", def->name, def->conditional_invocation->output);
        def->conditional_invocation->resolved_output = new_output;
        LOG_INFO("🔁 ConditionalInvocation Output rewritten: %s → %s", def->conditional_invocation->output, new_output);
    }

    // 🔁 Rewrite PlaceOfResolution sources (e.g., CI output signals)
    for (size_t i = 0; i < def->place_of_resolution_sources.count; ++i)
    {
        SourcePlace *sp = def->place_of_resolution_sources.items[i];
        if (!sp || !sp->name)
            continue;

        char *new_name;
        asprintf(&new_name, "%s.local.%s", def->name, sp->name);
        sp->resolved_name = new_name;
        LOG_INFO("🔁 POR (CI) SourcePlace rewritten: %s → %s", sp->name, new_name);
    }
}

void rewrite_por_invocations(Definition *def)
{
    if (!def || !def->place_of_resolution_invocations)
        return;

    StringSet *seen = create_string_set();
    StringSet *shared = create_string_set();

    // Step 1: Count signal name frequency to detect shared names
    for (Invocation *inv = def->place_of_resolution_invocations; inv; inv = inv->next)
    {
        for (size_t i = 0; i < inv->boundary_sources.count; ++i)
        {
            SourcePlace *sp = inv->boundary_sources.items[i];
            if (string_set_count(seen, sp->name))
                string_set_add_or_increment(shared, sp->name);
            else
                string_set_add_or_increment(seen, sp->name);
        }

        for (size_t i = 0; i < inv->boundary_destinations.count; ++i)
        {
            DestinationPlace *dp = inv->boundary_destinations.items[i];
            if (string_set_count(seen, dp->name))
                string_set_add_or_increment(shared, dp->name);
            else
                string_set_add_or_increment(seen, dp->name);
        }
    }

    for (size_t i = 0; i < def->boundary_sources.count; ++i)
    {
        SourcePlace *sp = def->boundary_sources.items[i];
        if (string_set_count(seen, sp->name))
            string_set_add_or_increment(shared, sp->name);
        else
            string_set_add_or_increment(seen, sp->name);
    }

    for (size_t i = 0; i < def->boundary_destinations.count; ++i)
    {
        DestinationPlace *dp = def->boundary_destinations.items[i];
        if (string_set_count(seen, dp->name))
            string_set_add_or_increment(shared, dp->name);
        else
            string_set_add_or_increment(seen, dp->name);
    }

    for (size_t i = 0; i < def->place_of_resolution_sources.count; ++i)
    {
        SourcePlace *por_src = def->place_of_resolution_sources.items[i];
        if (string_set_count(seen, por_src->name))
            string_set_add_or_increment(shared, por_src->name);
        else
            string_set_add_or_increment(seen, por_src->name);
    }

    // Step 2: Rewrite signal names
    for (Invocation *inv = def->place_of_resolution_invocations; inv; inv = inv->next)
    {
        int id = get_next_instance_id(inv->name);
        inv->instance_id = id;

        for (size_t i = 0; i < inv->boundary_sources.count; ++i)
        {
            SourcePlace *sp = inv->boundary_sources.items[i];
            char *new_name;
            if (string_set_count(shared, sp->name))
                asprintf(&new_name, "%s.local.%s", def->name, sp->name);
            else
                asprintf(&new_name, "%s.%s.%s", def->name, inv->name, sp->name);
            sp->resolved_name = new_name;
            LOG_INFO("🔁 POR SourcePlace rewritten: %s → %s", sp->name, new_name);
        }

        for (size_t i = 0; i < inv->boundary_destinations.count; ++i)
        {
            DestinationPlace *dp = inv->boundary_destinations.items[i];
            char *new_name;
            if (string_set_count(shared, dp->name))
                asprintf(&new_name, "%s.local.%s", def->name, dp->name);
            else
                asprintf(&new_name, "%s.%s.%s", def->name, inv->name, dp->name);
            dp->resolved_name = new_name;
            LOG_INFO("🔁 POR DestinationPlace rewritten: %s → %s", dp->name, new_name);
        }
    }

    destroy_string_set(seen);
    destroy_string_set(shared);
}

void rewrite_conditional_invocation(Definition *def)
{
    if (!def || !def->conditional_invocation)
        return;

    ConditionalInvocation *ci = def->conditional_invocation;

    if (!ci->resolved_template_args || ci->arg_count == 0)
    {
        LOG_WARN("⚠️ Definition %s: ConditionalInvocation missing template args", def->name);
        return;
    }

    for (size_t i = 0; i < ci->arg_count; ++i)
    {
        const char *arg = ci->resolved_template_args[i];
        if (!arg)
            continue;

        for (size_t j = 0; j < def->boundary_sources.count; ++j)
        {
            SourcePlace *sp = def->boundary_sources.items[j];
            if (sp->name && strcmp(sp->name, arg) == 0)
            {
                LOG_INFO("🔁 CI arg rewrite: %s.%zu → %s", def->name, i, sp->resolved_name);
                ci->resolved_template_args[i] = sp->resolved_name;
                break;
            }
        }
    }
}

void cleanup_name_counters(void)
{
    while (counters)
    {
        NameCounter *next = counters->next;
        free((void *)counters->name);
        free(counters);
        counters = next;
    }
}

int qualify_local_signals(Block *blk)
{
    LOG_INFO("🧪 Starting qualify_local_signals pass...");

    LOG_INFO("🔁 Rewriting top-level invocations...");
    rewrite_top_level_invocations(blk);

    for (Definition *def = blk->definitions; def != NULL; def = def->next)
    {
        LOG_INFO("📘 Processing Definition: %s", def->name);

        LOG_INFO("  🔤 Rewriting definition-level signal names...");
        rewrite_definition_signals(def);

        LOG_INFO("  🧠 Rewriting PlaceOfResolution invocations...");

        rewrite_por_invocations(def);

        LOG_INFO("🔧 Patch resolved_template_args for CI to use fully qualified signal names");
        rewrite_conditional_invocation(def);

        LOG_INFO("  🔌 Wiring outputs → POR sources...");
        //  wire_por_outputs_to_sources(def);

        LOG_INFO("  🔌 Wiring POR sources → outputs...");
        // wire_por_sources_to_outputs(def);
    }

    LOG_INFO("🧹 Cleaning up instance counters...");
    cleanup_name_counters();

    LOG_INFO("✅ Signal rewrite pass completed.");
    return 0;
}
