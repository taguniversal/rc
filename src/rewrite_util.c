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
void rewrite_literal_bindings(Block *blk);

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
/*
void rewrite_invocation(Invocation *inv)
{
    if (!inv || !inv->target_name) return;

    int id = get_next_instance_id(inv->target_name);
    inv->instance_id = id;

    for (size_t i = 0; i < string_list_count(inv->input_signals); ++i)
    {
        const char *name = string_list_get_by_index(inv->input_signals, i);
        if (!name) continue;

        char *new_name;
        asprintf(&new_name, "%s.%d.%s", inv->target_name, id, name);
        string_list_set_by_index(inv->input_signals, i, new_name);
        LOG_INFO("🔁 Invocation input rewritten: %s → %s", name, new_name);
    }

    for (size_t i = 0; i < string_list_count(inv->output_signals); ++i)
    {
        const char *name = string_list_get_by_index(inv->output_signals, i);
        if (!name) continue;

        char *new_name;
        asprintf(&new_name, "%s.%d.%s", inv->target_name, id, name);
        string_list_set_by_index(inv->output_signals, i, new_name);
        LOG_INFO("🔁 Invocation output rewritten: %s → %s", name, new_name);
    }
}
*/
void rewrite_invocation_for_instance(Instance *inst)
{
    if (!inst || !inst->invocation || !inst->name)
        return;

    Invocation *inv = inst->invocation;

    for (size_t i = 0; i < string_list_count(inv->input_signals); ++i)
    {
        const char *name = string_list_get_by_index(inv->input_signals, i);
        if (!name) continue;

        char *new_name;
        asprintf(&new_name, "%s.%s", inst->name, name);  // INV.XOR.0.X
        string_list_set_by_index(inv->input_signals, i, new_name);

        LOG_INFO("🔁 Input signal rewritten: %s → %s", name, new_name);
    }

    for (size_t i = 0; i < string_list_count(inv->output_signals); ++i)
    {
        const char *name = string_list_get_by_index(inv->output_signals, i);
        if (!name) continue;

        char *new_name;
        asprintf(&new_name, "%s.%s", inst->name, name);  // INV.XOR.0.OUT
        string_list_set_by_index(inv->output_signals, i, new_name);

        LOG_INFO("🔁 Output signal rewritten: %s → %s", name, new_name);
    }
}

void rewrite_definition_signals_for_instance(Instance *inst)
{
    if (!inst || !inst->definition || !inst->name)
        return;

    Definition *def = inst->definition;

    // 🔁 Rewrite input signal names
    for (size_t i = 0; i < string_list_count(def->input_signals); ++i)
    {
        const char *name = string_list_get_by_index(def->input_signals, i);
        if (!name) continue;

        char *new_name;
        asprintf(&new_name, "%s.local.%s", inst->name, name);
        string_list_set_by_index(def->input_signals, i, new_name);

        LOG_INFO("🔁 Input signal rewritten: %s → %s", name, new_name);
    }

    // 🔁 Rewrite output signal names
    for (size_t i = 0; i < string_list_count(def->output_signals); ++i)
    {
        const char *name = string_list_get_by_index(def->output_signals, i);
        if (!name) continue;

        char *new_name;
        asprintf(&new_name, "%s.local.%s", inst->name, name);
        string_list_set_by_index(def->output_signals, i, new_name);

        LOG_INFO("🔁 Output signal rewritten: %s → %s", name, new_name);
    }

    // 🔁 Rewrite signal names in BodyItem declarations
    for (BodyItem *item = def->body; item != NULL; item = item->next)
    {
        if (item->type == BODY_SIGNAL_INPUT || item->type == BODY_SIGNAL_OUTPUT)
        {
            if (item->data.signal_name)
            {
                char *new_name;
                asprintf(&new_name, "%s.local.%s", inst->name, item->data.signal_name);
                LOG_INFO("🔁 Body signal (%s) rewritten: %s → %s",
                         item->type == BODY_SIGNAL_INPUT ? "input" : "output",
                         item->data.signal_name, new_name);
                item->data.signal_name = new_name;
            }
        }
    }

    // 🔁 Rewrite ConditionalInvocation output
    if (def->conditional_invocation && def->conditional_invocation->output)
    {
        char *new_output;
        asprintf(&new_output, "%s.local.%s", inst->name, def->conditional_invocation->output);
        def->conditional_invocation->output = new_output;

        LOG_INFO("🔁 CI output rewritten → %s", new_output);
    }
}

void rewrite_conditional_invocation_for_instance(Instance *inst)
{
    if (!inst || !inst->definition || !inst->definition->conditional_invocation)
        return;

    ConditionalInvocation *ci = inst->definition->conditional_invocation;

    for (size_t i = 0; i < ci->arg_count; ++i)
    {
        const char *arg = string_list_get_by_index(ci->pattern_args, i);
        if (!arg) continue;

        char *original = strdup(arg);
        char *rewritten;
        asprintf(&rewritten, "%s.local.%s", inst->name, original);

        string_list_set_by_index(ci->pattern_args, i, rewritten);

        LOG_INFO("🔁 CI pattern arg[%zu] rewritten: %s → %s", i, original, rewritten);
        free(original);
    }

    LOG_INFO("✅ CI pattern args rewritten for: %s", inst->name);
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

void rewrite_literal_bindings_for_invocation(Invocation *inv, const char *instance_name)
{
    if (!inv || !instance_name || !inv->literal_bindings || inv->literal_bindings->count == 0)
    {
        LOG_INFO("ℹ️  No literal bindings to rewrite for instance '%s'", instance_name ? instance_name : "(null)");
        return;
    }

    char prefix[256];
    snprintf(prefix, sizeof(prefix), "%s.", instance_name);  // e.g., INV.AND.0.

    for (size_t i = 0; i < inv->literal_bindings->count; ++i)
    {
        LiteralBinding *b = &inv->literal_bindings->items[i];
        if (!b || !b->name)
            continue;

        LOG_INFO("  🔎 Before rewrite: bind(%s = %s)", b->name, b->value);

        char *qualified = NULL;
        asprintf(&qualified, "%s%s", prefix, b->name);

        free(b->name);
        b->name = qualified;

        LOG_INFO("  🔁 After rewrite:  bind(%s = %s)", b->name, b->value);
    }
}


int qualify_local_signals(Block *blk)
{
    LOG_INFO("🧪 Starting qualify_local_signals pass...");

    for (InstanceList *node = blk->instances; node; node = node->next)
    {
        Instance *inst = node->instance;
        if (!inst)
            continue;

        LOG_INFO("📦 Processing Instance: %s", inst->name);

        LOG_INFO("  🔁 Rewriting invocation signal names...");
        rewrite_invocation_for_instance(inst);

        LOG_INFO("  🔁 Rewriting literal bindings...");
        rewrite_literal_bindings_for_invocation(inst->invocation, inst->name);

        if (inst->definition)
        {
            LOG_INFO("  📘 Definition: %s", inst->definition->name);
            LOG_INFO("    🔤 Rewriting definition-level signals...");
            rewrite_definition_signals_for_instance(inst);

            LOG_INFO("    🔧 Patching resolved_template_args...");
            rewrite_conditional_invocation_for_instance(inst);
        }
    }

    LOG_INFO("🧹 Cleaning up instance counters...");
    cleanup_name_counters();

    LOG_INFO("✅ Signal rewrite pass completed.");
    return 0;
}
