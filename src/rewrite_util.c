#include "rewrite_util.h"
#include "log.h" // your logging macros
#include "log.h"
#include "eval_util.h"
#include "string_list.h"
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

void rewrite_invocations(Block *blk)
{
    // 🔁 Rewrite top-level block invocations
    for (Invocation *inv = blk->invocations; inv != NULL; inv = inv->next)
    {

        int id = get_next_instance_id(inv->target_name);
        inv->instance_id = id;
        for (size_t i = 0; i < string_list_count(inv->input_signals); ++i)
        {
            const char *name = string_list_get_by_index(inv->input_signals, i);
            if (!name)
                continue;
            char *new_name;
            asprintf(&new_name, "%s.%d.%s", inv->target_name, id, name);
            string_list_set_by_index(inv->input_signals, i, new_name);
            LOG_INFO("🔁 Top-level input signal rewritten: %s → %s", name, new_name);
        }

        for (size_t i = 0; i < string_list_count(inv->output_signals); ++i)
        {
            const char *name = string_list_get_by_index(inv->output_signals, i);
            if (!name)
                continue;
            char *new_name;
            asprintf(&new_name, "%s.%d.%s", inv->target_name, id, name);
            string_list_set_by_index(inv->output_signals, i, new_name);
            LOG_INFO("🔁 Top-level output signal rewritten: %s → %s", name, new_name);
        }
    }
    for (Definition *def = blk->definitions; def != NULL; def = def->next)
    {
        for (BodyItem *item = def->body; item != NULL; item = item->next)
        {
            if (item->type != BODY_INVOCATION || !item->data.invocation)
                continue;

            Invocation *inv = item->data.invocation;
            int id = get_next_instance_id(inv->target_name);
            inv->instance_id = id;

            for (size_t i = 0; i < string_list_count(inv->input_signals); ++i)
            {
                const char *name = string_list_get_by_index(inv->input_signals, i);
                if (!name)
                    continue;
                char *new_name;
                asprintf(&new_name, "%s.%d.%s", inv->target_name, id, name);
                string_list_set_by_index(inv->input_signals, i, new_name);
                LOG_INFO("🔁 Body input signal rewritten: %s → %s", name, new_name);
            }

            for (size_t i = 0; i < string_list_count(inv->output_signals); ++i)
            {
                const char *name = string_list_get_by_index(inv->output_signals, i);
                if (!name)
                    continue;
                char *new_name;
                asprintf(&new_name, "%s.%d.%s", inv->target_name, id, name);
                string_list_set_by_index(inv->output_signals, i, new_name);
                LOG_INFO("🔁 Body output signal rewritten: %s → %s", name, new_name);
            }
        }
    }
}

void rewrite_definition_signals(Definition *def)
{
    // 🔁 Rewrite input signal names
    for (size_t i = 0; i < string_list_count(def->input_signals); ++i)
    {
        const char *name = string_list_get_by_index(def->input_signals, i);
        if (!name)
            continue;

        char *new_name;
        asprintf(&new_name, "%s.local.%s", def->name, name);
        string_list_set_by_index(def->input_signals, i, new_name);
        LOG_INFO("🔁 Input signal rewritten: %s → %s", name, new_name);
    }

    // 🔁 Rewrite output signal names
    if (def->output_signals)
    {
        for (size_t group = 0; def->output_signals[group] != NULL; ++group)
        {
            StringList *group_list = def->output_signals[group];
            for (size_t j = 0; j < string_list_count(group_list); ++j)
            {
                const char *name = string_list_get_by_index(group_list, j);
                if (!name)
                    continue;

                char *new_name;
                asprintf(&new_name, "%s.local.%s", def->name, name);
                string_list_set_by_index(group_list, j, new_name);
                LOG_INFO("🔁 Output signal rewritten: %s → %s", name, new_name);
            }
        }
    }
    // 🔁 Rewrite signal names in BodyItem entries
    for (BodyItem *item = def->body; item != NULL; item = item->next)
    {
        if (item->type == BODY_SIGNAL_INPUT || item->type == BODY_SIGNAL_OUTPUT)
        {
            if (item->data.signal_name)
            {
                char *new_name;
                asprintf(&new_name, "%s.local.%s", def->name, item->data.signal_name);
                LOG_INFO("🔁 Body signal (%s) rewritten: %s → %s",
                         item->type == BODY_SIGNAL_INPUT ? "input" : "output",
                         item->data.signal_name, new_name);
                item->data.signal_name = new_name;
            }
        }
    }

    // 🔁 Rewrite ConditionalInvocation output (if present)
    if (def->conditional_invocation && def->conditional_invocation->output)
    {
        char *new_output;
        asprintf(&new_output, "%s.local.%s", def->name, def->conditional_invocation->output);
        def->conditional_invocation->output = new_output;
        LOG_INFO("🔁 ConditionalInvocation output rewritten → %s", new_output);
    }
}

void rewrite_conditional_invocation(Definition *def)
{
    if (!def || !def->conditional_invocation || def->conditional_invocation->arg_count == 0)
        return;

    ConditionalInvocation *ci = def->conditional_invocation;

    for (size_t i = 0; i < ci->arg_count; ++i)
    {
        const char *arg = ci->pattern_args[i];
        if (!arg)
            continue;

        char *rewritten;
        asprintf(&rewritten, "%s.local.%s", def->name, arg);

        free(ci->pattern_args[i]); // Free original string
        ci->pattern_args[i] = rewritten;

        LOG_INFO("🔁 CI pattern arg[%zu] rewritten: %s → %s", i, arg, rewritten);
    }

    LOG_INFO("✅ CI pattern args rewritten successfully for definition: %s", def->name);
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

    LOG_INFO("🔁 Rewriting invocations...");
    rewrite_invocations(blk);

    for (Definition *def = blk->definitions; def != NULL; def = def->next)
    {
        LOG_INFO("📘 Processing Definition: %s", def->name);

        LOG_INFO("  🔤 Rewriting definition-level signal names...");
        rewrite_definition_signals(def);

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
