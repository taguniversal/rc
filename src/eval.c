#include "log.h"
#include "block.h"
#include "block_util.h"
#include "eval.h"
#include "eval_util.h"
#include "instance.h"
#include "string_list.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_ITERATIONS 5

static int evaluate_conditional_logic(Instance *inst)
{
    Definition *def = inst->definition;
    Invocation *inv = inst->invocation;

    if (!def || !inv || !def->conditional_invocation)
        return 0;

    LOG_INFO("🔘 Evaluating conditional logic for: %s", def->name);

    // Build pattern from literal bindings
    char pattern[256] = {0};

    for (size_t i = 0; i < def->conditional_invocation->case_count; ++i)
    {
        const char *sig = string_list_get_by_index(def->input_signals, i);
        if (!sig) continue;

        for (size_t j = 0; j < inv->literal_bindings->count; ++j)
        {
            LiteralBinding lb = inv->literal_bindings->items[j];
            if (strcmp(lb.name, sig) == 0)
            {
                strncat(pattern, lb.value, sizeof(pattern) - strlen(pattern) - 1);
                break;
            }
        }
    }

    LOG_INFO("🔣 Generated pattern: %s", pattern);

    // Match against cases
    for (size_t i = 0; i < def->conditional_invocation->case_count; ++i)
    {
        ConditionalCase c = def->conditional_invocation->cases[i];
        if (strcmp(c.pattern, pattern) == 0)
        {
            LOG_INFO("📤 Matched result: %s", c.result);
            // You can store this output result somewhere meaningful here
            return 1;
        }
    }

    LOG_WARN("⚠️ No matching case for pattern: %s", pattern);
    return 0;
}

int eval_instance(Instance *instance, Block *blk)
{
    if (!instance || !instance->definition || !instance->invocation)
    {
        LOG_WARN("⚠️ Skipping incomplete unit.");
        return 0;
    }

    LOG_INFO("🔍 Evaluating instance: %s", instance->invocation->target_name);
    int changed = evaluate_conditional_logic((Instance *)instance);
    LOG_INFO("✅ Done evaluating: %s", instance->invocation->target_name);
    return changed;
}

int eval(Block *blk)
{
    int total_changes = 0;
    int iteration = 0;

    LOG_INFO("🔁 Starting evaluation loop (max %d iterations)", MAX_ITERATIONS);

    do {
        int changes_this_round = 0;

        for (InstanceList *inst = blk->instances; inst != NULL; inst = inst->next)
        {
            Instance temp = {
                .definition = inst->instance->definition,
                .invocation = inst->instance->invocation
            };
            changes_this_round += eval_instance(&temp, blk);
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

    } while (1);

    LOG_INFO("🧮 Total changes: %d", total_changes);
    return total_changes;
}
