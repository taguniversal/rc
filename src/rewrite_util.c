#include "rewrite_util.h"
#include "log.h" // your logging macros
#include "log.h"
#include "eval_util.h"
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

        for (SourcePlace *src = inv->sources; src != NULL; src = src->next)
        {
            char *new_name;
            asprintf(&new_name, "%s.%d.%s", inv->name, id, src->name);
            src->resolved_name = new_name;
            LOG_INFO("🔁 SourcePlace rewritten: %s → %s", src->name, new_name);
        }

        int param_index = 0;
        for (DestinationPlace *dst = inv->destinations; dst != NULL; dst = dst->next, param_index++)
        {
            if (!dst->name)
            {
                char synth[64];
                snprintf(synth, sizeof(synth), "%s.%d.%d", inv->name, id, param_index);
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
    for (SourcePlace *src = def->sources; src != NULL; src = src->next)
    {
        char *new_name;
        asprintf(&new_name, "%s.local.%s", def->name, src->name);
        src->resolved_name = new_name;
        LOG_INFO("🔁 Definition SourcePlace rewritten: %s → %s", src->name, new_name);
    }

    for (DestinationPlace *dst = def->destinations; dst != NULL; dst = dst->next)
    {
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
}

void rewrite_por_invocations(Definition *def)
{
    for (Invocation *inv = def->place_of_resolution_invocations; inv != NULL; inv = inv->next)
    {
        int id = get_next_instance_id(inv->name);
        inv->instance_id = id;

        for (SourcePlace *src = inv->sources; src != NULL; src = src->next)
        {
            char *new_name;
            asprintf(&new_name, "%s.%s.%s", def->name, inv->name, src->name);
            src->resolved_name = new_name;
            LOG_INFO("🔁 POR SourcePlace rewritten: %s → %s", src->name, new_name);
        }

        for (DestinationPlace *dst = inv->destinations; dst != NULL; dst = dst->next)
        {
            char *new_name;
            asprintf(&new_name, "%s.%s.%s", def->name, inv->name, dst->name);
            dst->resolved_name = new_name;
            LOG_INFO("🔁 POR DestinationPlace rewritten: %s → %s", dst->name, new_name);
        }
    }

    for (Invocation *inv = def->invocations; inv != NULL; inv = inv->next)
    {
        for (SourcePlace *src = inv->sources; src != NULL; src = src->next)
        {
            char *new_name;
            asprintf(&new_name, "%s.%s.%s", def->name, inv->name, src->name);
            src->resolved_name = new_name;
            LOG_INFO("🔁 Nested SourcePlace rewritten: %s → %s", src->name, new_name);
        }

        for (DestinationPlace *dst = inv->destinations; dst != NULL; dst = dst->next)
        {
            char *new_name;
            asprintf(&new_name, "%s.%s.%s", def->name, inv->name, dst->name);
            dst->resolved_name = new_name;
            LOG_INFO("🔁 Nested DestinationPlace rewritten: %s → %s", dst->name, new_name);
        }
    }

    // Patch: rewrite def->place_of_resolution_sources
    for (SourcePlace *por_src = def->place_of_resolution_sources; por_src; por_src = por_src->next)
    {
        if (por_src->name)
        {
            char *new_name;
            asprintf(&new_name, "%s.local.%s", def->name, por_src->name); // or your preferred naming
            por_src->resolved_name = new_name;
            LOG_INFO("🔁 POR SourcePlace (loose) rewritten: %s → %s", por_src->name, new_name);
        }
        else
        {
            LOG_WARN("⚠️ POR SourcePlace in def %s has null name", def->name);
        }
    }
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

        for (SourcePlace *sp = def->sources; sp; sp = sp->next)
        {
            if (sp->name && strcmp(sp->name, arg) == 0)
            {
                LOG_INFO("🔁 CI arg rewrite: %s.%zu → %s", def->name, i, sp->resolved_name);
                ci->resolved_template_args[i] = sp->resolved_name;
                break;
            }
        }
    }
}
void wire_por_outputs_to_sources(Definition *def)
{
    for (Invocation *inv = def->invocations; inv; inv = inv->next)
    {
        for (DestinationPlace *dst = inv->destinations; dst; dst = dst->next)
        {
            for (SourcePlace *por_src = def->place_of_resolution_sources; por_src; por_src = por_src->next)
            {
                if (dst->resolved_name && por_src->resolved_name &&
                    strcmp(dst->resolved_name, por_src->resolved_name) == 0)
                {
                    if (dst->content && !por_src->content)
                    {
                        por_src->content = strdup(dst->content);
                        LOG_INFO("🔁 Back-copy: POR Source '%s' gets content from Invocation '%s' Destination '%s' → [%s]",
                                 por_src->resolved_name, inv->name, dst->resolved_name, por_src->content);
                    }
                    else if (!dst->content && por_src->content)
                    {
                        dst->content = strdup(por_src->content);
                        LOG_INFO("🔁 Forward-copy: Invocation '%s' Destination '%s' gets content from POR Source '%s' → [%s]",
                                 inv->name, dst->resolved_name, por_src->resolved_name, dst->content);
                    }
                    else if (dst->content && por_src->content &&
                             strcmp(dst->content, por_src->content) != 0)
                    {
                        LOG_WARN("⚠️ Mismatch: Invocation '%s' Destination '%s' and POR Source '%s' have different contents! (%s vs %s)",
                                 inv->name, dst->resolved_name, por_src->resolved_name,
                                 dst->content, por_src->content);
                    }
                }
            }
        }
    }
}
void wire_por_sources_to_outputs(Definition *def)
{
    for (DestinationPlace *def_dst = def->destinations; def_dst; def_dst = def_dst->next)
    {
        for (SourcePlace *por_src = def->place_of_resolution_sources; por_src; por_src = por_src->next)
        {
            if (def_dst->resolved_name && por_src->resolved_name &&
                strcmp(def_dst->resolved_name, por_src->resolved_name) == 0)
            {
                if (def_dst->content && !por_src->content)
                {
                    por_src->content = strdup(def_dst->content);
                    LOG_INFO("🔁 Back-copy: POR Source '%s' gets content from Definition Destination '%s' → [%s]",
                             por_src->resolved_name, def_dst->resolved_name, por_src->content);
                }
                else if (!def_dst->content && por_src->content)
                {
                    def_dst->content = strdup(por_src->content);
                    LOG_INFO("🔁 Forward-copy: Definition Destination '%s' gets content from POR Source '%s' → [%s]",
                             def_dst->resolved_name, por_src->resolved_name, def_dst->content);
                }
                else if (def_dst->content && por_src->content &&
                         strcmp(def_dst->content, por_src->content) != 0)
                {
                    LOG_WARN("⚠️ Mismatch: Definition Destination '%s' and POR Source '%s' have different contents! (%s vs %s)",
                             def_dst->resolved_name, por_src->resolved_name,
                             def_dst->content, por_src->content);
                }
            }
        }
    }
}

void wire_invocation_sources_to_definition_outputs(Block *blk)
{
    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
        Definition *def = inv->definition;
        if (!def)
            continue;

        for (SourcePlace *src = inv->sources; src; src = src->next)
        {
            for (DestinationPlace *def_dst = def->destinations; def_dst; def_dst = def_dst->next)
            {
                // Match logical name of the output with unresolved invocation source name
                if (!src->content && src->name && def_dst->name &&
                    strcmp(src->name, def_dst->name) == 0)
                {
                    if (def_dst->content)
                    {
                        src->content = strdup(def_dst->content);
                        LOG_INFO("🔁 Top-level SourcePlace '%s' (Invocation '%s') copied from Definition output '%s' → [%s]",
                                 src->name, inv->name, def_dst->name, src->content);
                    }
                    else
                    {
                        LOG_WARN("⚠️ DestinationPlace '%s' in Definition '%s' has no content to copy",
                                 def_dst->name, def->name);
                    }
                }
            }
        }
    }
}

void wire_por_invocations(Block *blk)
{
    LOG_INFO("🧠 Entered wire_por_invocations()");

    if (!blk)
    {
        LOG_WARN("🚫 wire_por_invocations: Block is NULL");
        return;
    }

    for (Definition *def = blk->definitions; def; def = def->next)
    {
        if (!def->place_of_resolution_invocations)
        {
            LOG_INFO("🧩 Definition %s has no POR invocations", def->name);
            continue;
        }
        for (Invocation *inv = def->place_of_resolution_invocations; inv; inv = inv->next)
        {
            // Step 1: Wire Definition.sources → Invocation.destinations
            LOG_INFO("🧩 Wiring definition %s sources to invocation %s destinations", def->name, inv->name);
            for (SourcePlace *def_src = def->sources; def_src; def_src = def_src->next)
            {

                for (DestinationPlace *inv_dst = inv->destinations; inv_dst; inv_dst = inv_dst->next)
                {
                    LOG_INFO("🧩 Definition source %s -> invocation destination %s", def_src->resolved_name, inv_dst->resolved_name);
                    if (def_src->resolved_name && inv_dst->resolved_name &&
                        strcmp(def_src->resolved_name, inv_dst->resolved_name) == 0 &&
                        def_src->content)
                    {
                        propagate_content(def_src, inv_dst);

                        LOG_INFO("🔗 [POR] Wired Definition → Invocation: %s → %s [%s]",
                                 def_src->resolved_name, inv_dst->resolved_name, inv_dst->content);
                    }
                }
            }

            // Step 2: Wire Invocation.sources → Definition.destinations
            for (SourcePlace *inv_src = inv->sources; inv_src; inv_src = inv_src->next)
            {
                for (DestinationPlace *def_dst = def->destinations; def_dst; def_dst = def_dst->next)
                {
                    if (inv_src->resolved_name && def_dst->resolved_name &&
                        strcmp(inv_src->resolved_name, def_dst->resolved_name) == 0 &&
                        inv_src->content)
                    {
                        propagate_content(inv_src, def_dst);
                        LOG_INFO("🔗 [POR] Wired Invocation → Definition: %s → %s [%s]",
                                 inv_src->resolved_name, def_dst->resolved_name, def_dst->content);
                    }
                }
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
