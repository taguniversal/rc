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
            update_signal_value(signal_map, ci->output, c.result);
            publish_signal(ci->output, c.result);
            return 1;
        }
    }

    LOG_WARN("⚠️ No matching case for pattern: %s", pattern);
    return 0;
}

void propagate_inputs_to_definition(Instance *instance, SignalMap *map)
{
    if (!instance || !instance->invocation || !instance->definition)
        return;

    StringList *inv_inputs = instance->invocation->input_signals;
    StringList *def_inputs = instance->definition->input_signals;

    if (!inv_inputs || !def_inputs || inv_inputs->size != def_inputs->size)
    {
        LOG_WARN("⚠️ Input signal count mismatch for instance: %s", instance->invocation->target_name);
        return;
    }

    for (size_t i = 0; i < inv_inputs->size; ++i)
    {
        const char *inv_name = string_list_get_by_index(inv_inputs, i);
        const char *def_name = string_list_get_by_index(def_inputs, i);

        if (!inv_name || !def_name)
        {
            LOG_WARN("⚠️ Null signal name at index %zu in instance: %s", i, instance->invocation->target_name);
            continue;
        }

        const char *value = get_signal_value(map, inv_name);
        if (value)
        {
            update_signal_value(map, def_name, value);
            LOG_INFO("🔄 Copied signal: %s → %s = %s", inv_name, def_name, value);
        }
        else
        {
            LOG_WARN("⚠️ Signal value not found for: %s", inv_name);
        }
    }
}

void propagate_outputs_to_invocation(Instance *instance, SignalMap *map)
{
    if (!instance || !instance->invocation || !instance->definition)
        return;

    StringList *def_outputs = instance->definition->output_signals;
    StringList *inv_outputs = instance->invocation->output_signals;

    if (!def_outputs || !inv_outputs)
    {
        LOG_WARN("⚠️ Missing output signals in instance: %s — def_outputs=%p, inv_outputs=%p", instance->invocation->target_name, def_outputs, inv_outputs);
        return;
    }

    if (def_outputs->size != inv_outputs->size)
    {
        LOG_WARN("⚠️ Output signal count mismatch for instance: %s", instance->invocation->target_name);
        LOG_WARN("    📦 Definition outputs (%zu):", def_outputs->size);
        for (size_t i = 0; i < def_outputs->size; ++i)
        {
            LOG_WARN("      🔸 %s", string_list_get_by_index(def_outputs, i));
        }

        LOG_WARN("    📦 Invocation outputs (%zu):", inv_outputs->size);
        for (size_t i = 0; i < inv_outputs->size; ++i)
        {
            LOG_WARN("      🔹 %s", string_list_get_by_index(inv_outputs, i));
        }
        return;
    }

    for (size_t i = 0; i < def_outputs->size; ++i)
    {
        const char *def_name = string_list_get_by_index(def_outputs, i);
        const char *inv_name = string_list_get_by_index(inv_outputs, i);

        if (!def_name || !inv_name)
        {
            LOG_WARN("⚠️ Null signal name at index %zu in instance: %s", i, instance->invocation->target_name);
            continue;
        }

        const char *value = get_signal_value(map, def_name);
        if (value)
        {
            update_signal_value(map, inv_name, value);
            LOG_INFO("🔁 Copied signal: %s → %s = %s", def_name, inv_name, value);
        }
        else
        {
            LOG_WARN("⚠️ Signal value not found for: %s", def_name);
        }
    }
}

int eval_instance(Instance *instance, Block *blk, SignalMap *signal_map)
{
    if (!instance || !instance->definition || !instance->invocation)
    {
        LOG_WARN("⚠️ Skipping incomplete unit.");
        return 0;
    }

    // Publish any literal bindings to signal map
    Invocation *inv = instance->invocation;
    if (inv->literal_bindings)
    {
        for (size_t i = 0; i < inv->literal_bindings->count; ++i)
        {
            LiteralBinding *binding = &inv->literal_bindings->items[i];
            if (!binding->name || !binding->value)
                continue;

            update_signal_value(signal_map, binding->name, binding->value);
            LOG_INFO("📥 Published literal: %s = %s", binding->name, binding->value);
        }
    }

    propagate_inputs_to_definition(instance, signal_map);

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

    propagate_outputs_to_invocation(instance, signal_map);

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
