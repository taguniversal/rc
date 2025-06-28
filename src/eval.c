#include "log.h"
#include "block.h"
#include "block_util.h"
#include "eval.h"
#include "eval_util.h"
#include "instance.h"
#include "string_list.h"
#include "signal_map.h"
#include "signal.h"
#include "pubsub.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define MAX_ITERATIONS 5

int evaluate_conditional_logic(Instance *inst, SignalMap *signal_map)
{
    if (!inst || !inst->definition || !inst->invocation)
        return 0;

    Definition *def = inst->definition;
    Invocation *inv = inst->invocation;
    ConditionalInvocation *ci = def->conditional_invocation;

    if (!ci || !ci->pattern_args || ci->arg_count == 0 || !ci->output)
        return 0;

    LOG_INFO("🔘 Evaluating conditional logic for: %s", def->name);

    // 1. Build pattern from actual signal values
    char pattern[256] = {0};

    LOG_INFO("🧵 Pattern args (%zu):", ci->arg_count);
    for (size_t i = 0; i < ci->arg_count; ++i)
    {
        const char *sig = string_list_get_by_index(ci->pattern_args, i);
        LOG_INFO("    [%zu] %s", i, sig ? sig : "(null)");
    }

    for (size_t i = 0; i < ci->arg_count; ++i)
    {
        const char *sig = string_list_get_by_index(ci->pattern_args, i);
        if (!sig)
            continue;

        const char *value = get_signal_value(signal_map, sig);
        if (!value)
        {
            LOG_WARN("🚫 Signal '%s' not found in signal map", sig);
            return 0; // 🚫 Bail out early if a signal is missing
        }

        strncat(pattern, value, sizeof(pattern) - strlen(pattern) - 1);
    }

    LOG_INFO("🔣 Generated pattern: %s", pattern);

    // 2. Match against conditional cases
    for (size_t i = 0; i < ci->case_count; ++i)
    {
        ConditionalCase c = ci->cases[i];
        if (strcmp(c.pattern, pattern) == 0)
        {
            LOG_INFO("📤 Matched result: %s → Publishing to %s", c.result, ci->output);
            publish_signal(signal_map, ci->output, c.result);
            return 1;
        }
    }

    LOG_WARN("⚠️ No matching case for pattern: %s", pattern);
    return 0;
}


int eval_instance(Instance *instance, Block *blk, SignalMap *signal_map)
{
    if (!instance || !instance->definition || !instance->invocation)
    {
        LOG_WARN("⚠️ Skipping incomplete unit.");
        return 0;
    }

    LOG_INFO("Evaluating instance %s", instance->name);

    // Publish any literal bindings to signal map
    Invocation *inv = instance->invocation;
    if (inv->literal_bindings)
    {
        for (size_t i = 0; i < inv->literal_bindings->count; ++i)
        {
            LiteralBinding *binding = &inv->literal_bindings->items[i];

            if (!binding->name)
                LOG_WARN("⚠️ NULL binding name at index %zu", i);
            if (!binding->value)
                LOG_WARN("⚠️ NULL binding value at index %zu", i);

            if (!binding->name || !binding->value)
                continue;

            update_signal_value(signal_map, binding->name, binding->value);
            LOG_INFO("📥 Published literal: %s = %s", binding->name, binding->value);
        }
    }

    // Now check if inputs are ready
    StringList *input_names = inv->input_signals;
    if (!all_signals_ready(input_names, signal_map))
    {
        LOG_INFO("⏳ Skipping %s — inputs not ready", inv->target_name);
        return 0;
    }

    LOG_INFO("🔍 Evaluating instance: %s", inv->target_name);
    int changed = evaluate_conditional_logic(instance, signal_map);
    LOG_INFO("✅ Done evaluating: %s", inv->target_name);

    return changed;
}

int eval(Block *blk, SignalMap *signal_map)
{
    int total_changes = 0;
    int iteration = 0;

    LOG_INFO("🔁 Starting evaluation loop (max %d iterations)", MAX_ITERATIONS);

    do
    {
        int changes_this_round = 0;
        for (InstanceList *node = blk->instances; node != NULL; node = node->next)
        {
            Instance *inst = node->instance;

            if (!inst)
                LOG_WARN("⚠️ NULL instance found in list.");
            else if (!inst->definition)
                LOG_WARN("⚠️ Instance missing definition: %s", inst->name ? inst->name : "(null)");
            else if (!inst->invocation)
                LOG_WARN("⚠️ Instance missing invocation: %s", inst->name ? inst->name : "(null)");

            changes_this_round += eval_instance(inst, blk, signal_map);
        }

        total_changes += changes_this_round;
        iteration++;

        if (changes_this_round == 0)
        {
            LOG_INFO("🟢 Stable — no changes detected.");
            break;
        }

        if (iteration >= MAX_ITERATIONS)
        {
            LOG_WARN("⚠️ Max iterations reached. Evaluation incomplete or unstable.");
            break;
        }

        poll_pubsub(signal_map);

    } while (1);

    LOG_INFO("🧮 Total changes: %d", total_changes);
    return total_changes;
}
