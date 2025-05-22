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
