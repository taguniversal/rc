#include "log.h"
#include "sqlite3.h"
#include "util.h"
#include "wiring.h"
#include "eval_util.h"
#include "eval.h"
#include "block_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#define MAX_ITERATIONS 5 // Or whatever feels safe for your app

#include "eval.h"
#include "block.h"

int eval(Block *blk);
int pull_outputs_from_definition(Block *blk, Invocation *inv);
DestinationPlace *find_output_by_resolved_name(const char *resolved_name, Definition *def);

int boundary_link_invocations_by_position(Block *blk)
{
    int side_effects = 0;

    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
        for (Definition *def = blk->definitions; def; def = def->next)
        {
            if (strcmp(inv->name, def->name) == 0)
            {
                inv->definition = def;
                LOG_INFO("🔗 Linked Invocation %s → Definition %s", inv->name, def->name);

                // Step 1: Copy content from POR Source → Definition Destinations
                if (def->boundary_destinations.count > 0 && def->place_of_resolution_sources.count > 0)
                {
                    for (size_t d = 0; d < def->boundary_destinations.count; ++d)
                    {
                        DestinationPlace *def_dst = def->boundary_destinations.items[d];
                        for (size_t s = 0; s < def->place_of_resolution_sources.count; ++s)
                        {
                            SourcePlace *por_src = def->place_of_resolution_sources.items[s];

                            if (def_dst->resolved_name && por_src->resolved_name &&
                                strcmp(def_dst->resolved_name, por_src->resolved_name) == 0)
                            {
                                if (por_src->content)
                                {
                                    if (!def_dst->content || strcmp(def_dst->content, por_src->content) != 0)
                                    {
                                        if (def_dst->content)
                                            free(def_dst->content);
                                        def_dst->content = strdup(por_src->content);
                                        side_effects++;
                                        LOG_INFO("🔁 Name-copy: Destination '%s' gets content from POR Source '%s' → [%s]",
                                                 def_dst->resolved_name, por_src->resolved_name, def_dst->content);
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    LOG_WARN("🚨 Missing destinations or POR sources for def=%s", def->name);
                }

                // Step 3: Transfer outputs from Definition Destinations → Invocation Sources
                size_t count = MIN(inv->boundary_sources.count, def->boundary_destinations.count);
                for (size_t i = 0; i < count; ++i)
                {
                    SourcePlace *inv_src = inv->boundary_sources.items[i];
                    DestinationPlace *def_dst = def->boundary_destinations.items[i];

                    if (def_dst->content)
                    {
                        if (!inv_src->content || strcmp(inv_src->content, def_dst->content) != 0)
                        {
                            side_effects += propagate_content(inv_src, def_dst);
                            LOG_INFO("🔌 [OUT] [%zu] %s ← %s [%s]", i,
                                     inv_src->resolved_name, def_dst->resolved_name, def_dst->content);
                        }
                    }
                }

                break; // Stop after matching definition
            }
        }

        if (!inv->definition)
        {
            LOG_WARN("⚠️ No matching Definition found for Invocation %s", inv->name);
        }
    }

    return side_effects;
}

void transfer_invocation_inputs_to_definition(Invocation *inv, Definition *def)
{
    if (!inv || !def)
        return;

    size_t count = (inv->boundary_destinations.count < def->boundary_sources.count)
                       ? inv->boundary_destinations.count
                       : def->boundary_sources.count;

    for (size_t idx = 0; idx < count; ++idx)
    {
        DestinationPlace *inv_dst = inv->boundary_destinations.items[idx];
        SourcePlace *def_src = def->boundary_sources.items[idx];

        if (!inv_dst || !def_src)
            continue;

        if (inv_dst->content)
        {
            if (def_src->content)
                free(def_src->content);

            def_src->content = strdup(inv_dst->content);
            LOG_INFO("🔁 [IN] Transfer #%zu: %s → %s [%s]",
                     idx,
                     inv_dst->resolved_name ? inv_dst->resolved_name : "(null)",
                     def_src->resolved_name ? def_src->resolved_name : "(null)",
                     def_src->content);
        }
        else
        {
            LOG_WARN("⚠️ [IN] Input %zu: Invocation destination '%s' is empty",
                     idx, inv_dst->resolved_name ? inv_dst->resolved_name : "(null)");
        }
    }

    if (inv->boundary_destinations.count != def->boundary_sources.count)
    {
        LOG_WARN("⚠️ [IN] Mismatched input lengths during transfer: invocation.destinations=%zu, def.sources=%zu",
                 inv->boundary_destinations.count, def->boundary_sources.count);
    }
}

char *build_conditional_pattern(ConditionalInvocation *ci, Block *blk)
{
    if (!ci || !ci->resolved_template_args || ci->arg_count == 0)
    {
        LOG_ERROR("❌ Invalid ConditionalInvocation or missing arguments");
        return NULL;
    }

    // First, determine total pattern length
    size_t total_len = 0;
    for (size_t i = 0; i < ci->arg_count; ++i)
    {
        const char *arg_name = ci->resolved_template_args[i];
        SourcePlace *src = find_source(blk, arg_name);

        if (src && src->content)
            total_len += strlen(src->content);
        else
            total_len += 1; // Use "?" placeholder if missing
    }

    // Allocate string (+1 for null terminator)
    char *pattern = malloc(total_len + 1);
    if (!pattern)
    {
        LOG_ERROR("❌ Memory allocation failed for pattern");
        return NULL;
    }

    pattern[0] = '\0'; // Start with empty string

    // Append each content (or "?" if not found)
    for (size_t i = 0; i < ci->arg_count; ++i)
    {
        const char *arg_name = ci->resolved_template_args[i];
        SourcePlace *src = find_source(blk, arg_name);

        const char *content = (src && src->content) ? src->content : "?";
        strcat(pattern, content);
    }

    LOG_INFO("🧩 Built conditional pattern: %s", pattern);
    return pattern;
}
// Original logic of eval_unit, now broken into clearer helper functions
#include "eval_util.h"
#include "log.h"

static int transfer_inputs_from_invocation_to_definition(Invocation *inv, Definition *def)
{
    LOG_INFO("🔁 Step 1: Transferring invocation inputs → definition");
    transfer_invocation_inputs_to_definition(inv, def);
    return 0;
}

static int evaluate_conditional_invocation(Invocation *inv, Definition *def, Block *blk)
{
    int change_count = 0;
    if (!def->conditional_invocation)
        return 0;

    LOG_INFO("🔘 Step 2: Running conditional logic for unit: %s", inv->name);
    const char *pattern = build_conditional_pattern(def->conditional_invocation, blk);
    if (!pattern)
    {
        LOG_ERROR("❌ Failed to build pattern for ConditionalInvocation.");
        return 0;
    }

    LOG_INFO("🧩 Built pattern: %s", pattern);
    const char *result = match_conditional_case(def->conditional_invocation, pattern);
    free((void *)pattern);

    if (!result)
    {
        LOG_WARN("⚠️ No match found for ConditionalInvocation pattern.");
        return 0;
    }

    LOG_INFO("📤 Match result: %s", result);
    const char *output_name = def->conditional_invocation->resolved_output;
    LOG_INFO("📦 Output name for conditional result: %s", output_name);

    DestinationPlace *dst = find_destination(blk, output_name);
    if (dst)
    {
        if (dst->content)
            free(dst->content);
        dst->content = strdup(result);
        LOG_INFO("✅ Wrote result to block-level output: %s = %s", output_name, dst->content);
        change_count++;
    }
    else
    {
        LOG_ERROR("❌ Failed to find DestinationPlace for output name: %s", output_name);
    }

    for (size_t i = 0; i < inv->boundary_sources.count; ++i)
    {
        SourcePlace *sp = inv->boundary_sources.items[i];
        if (sp && sp->resolved_name && strcmp(sp->resolved_name, output_name) == 0)
        {
            if (sp->content)
                free(sp->content);
            sp->content = strdup(result);
            LOG_INFO("🔁 Bound invocation output: %s = %s", sp->resolved_name, sp->content);
            change_count++;
        }
    }
    return change_count;
}

static int propagate_internal_por_signals(Definition *def, Block *blk)
{
    LOG_INFO("🔁 Step 3: Stabilizing internal signals within definition");
    int total_change_count = 0;

    bool changed;
    int iteration = 0;

    do {
        changed = false;
        iteration++;
        LOG_INFO("🔄 POR Iteration #%d", iteration);

        // Re-evaluate all conditional invocations in the definition
        for (Invocation *a = def->place_of_resolution_invocations; a; a = a->next)
        {
            if (a->definition && a->definition->conditional_invocation)

            {
                const char *pattern = build_conditional_pattern(a->definition->conditional_invocation, blk);
                if (!pattern)
                {
                    LOG_ERROR("❌ Failed to build pattern for POR ConditionalInvocation.");
                    continue;
                }

                LOG_INFO("🧩 [POR] Built pattern: %s", pattern);
                const char *result = match_conditional_case(a->definition->conditional_invocation, pattern);
                free((void *)pattern);

                if (result)
                {
                    const char *output_name = a->definition->conditional_invocation->resolved_output;
                    LOG_INFO("📤 [POR] Match result: %s → %s", pattern, output_name);

                    for (size_t i = 0; i < a->boundary_sources.count; ++i)
                    {
                        SourcePlace *sp = a->boundary_sources.items[i];
                        if (!sp || !sp->resolved_name)
                            continue;
                        if (strcmp(sp->resolved_name, output_name) == 0)
                        {
                            if (!sp->content || strcmp(sp->content, result) != 0)
                            {
                                if (sp->content)
                                    free(sp->content);
                                sp->content = strdup(result);
                                LOG_INFO("🔁 [POR] Bound invocation output: %s = %s", sp->resolved_name, sp->content);
                                changed = true;
                                total_change_count++;
                            }
                        }
                    }
                }
                else
                {
                    LOG_WARN("⚠️ [POR] No match found for pattern.");
                }
            }
        }

        // Propagate values between invocations
        for (Invocation *a = def->place_of_resolution_invocations; a; a = a->next)
        {
            for (size_t i = 0; i < a->boundary_sources.count; ++i)
            {
                SourcePlace *src = a->boundary_sources.items[i];
                if (!src || !src->resolved_name || !src->content)
                    continue;

                for (Invocation *b = def->place_of_resolution_invocations; b; b = b->next)
                {
                    for (size_t j = 0; j < b->boundary_destinations.count; ++j)
                    {
                        DestinationPlace *dst = b->boundary_destinations.items[j];
                        if (!dst || !dst->resolved_name)
                            continue;

                        if (strcmp(src->resolved_name, dst->resolved_name) == 0)
                        {
                            int propagated = propagate_content(src, dst);
                            if (propagated > 0)
                            {
                                LOG_INFO("🔌 [POR] Propagated: %s → %s", src->resolved_name, dst->resolved_name);
                                changed = true;
                                total_change_count += propagated;
                            }
                        }
                    }
                }
            }
        }

    } while (changed && iteration < 10); // safety bound

    return total_change_count;
}


static int push_outputs_from_invocation_to_block(Invocation *inv, Block *blk)
{
    LOG_INFO("⬆️ Step 4: Propagating unit outputs → block-level destinations");
    int change_count = 0;
    for (size_t i = 0; i < inv->boundary_sources.count; ++i)
    {
        SourcePlace *src = inv->boundary_sources.items[i];
        if (!src || !src->resolved_name || !src->content)
            continue;

        SourcePlace *blk_src = find_source(blk, src->resolved_name);
        if (blk_src && (!blk_src->content || strcmp(blk_src->content, src->content) != 0))
        {
            if (blk_src->content)
                free(blk_src->content);
            blk_src->content = strdup(src->content);
            LOG_INFO("🌍 Synced global source: %s = %s", blk_src->resolved_name, blk_src->content);
            change_count++;
        }

        DestinationPlace *dst = find_destination(blk, src->resolved_name);
        if (dst)
        {
            LOG_INFO("📤 Writing unit output %s to block", src->resolved_name);
            change_count += propagate_content(src, dst);
        }
    }
    return change_count;
}

static int pull_inputs_from_block_to_invocation(Invocation *inv, Block *blk)
{
    LOG_INFO("⬇️ Step 5: Pulling block-level sources → unit inputs");
    int change_count = 0;
    for (size_t i = 0; i < inv->boundary_destinations.count; ++i)
    {
        DestinationPlace *dst = inv->boundary_destinations.items[i];
        if (!dst || !dst->resolved_name)
            continue;

        SourcePlace *src = find_source(blk, dst->resolved_name);
        if (src && src->content)
        {
            LOG_INFO("📥 Feeding %s from block to unit input", dst->resolved_name);
            change_count += propagate_content(src, dst);
        }
    }
    return change_count;
}

int eval_unit(Unit *unit, Block *blk)
{
    if (!unit->definition || !unit->invocation)
    {
        LOG_WARN("⚠️ Unit missing definition or invocation: %s", unit->name);
        return 0;
    }

    LOG_INFO("🔍 Evaluating unit: %s", unit->name);
    int changes = 0;
    
    changes += pull_inputs_from_block_to_invocation(unit->invocation, blk); // move this ↑
    changes += transfer_inputs_from_invocation_to_definition(unit->invocation, unit->definition);
    changes += evaluate_conditional_invocation(unit->invocation, unit->definition, blk);
    changes += propagate_internal_por_signals(unit->definition, blk);
    changes += push_outputs_from_invocation_to_block(unit->invocation, blk);
    
    LOG_INFO("✅ Finished evaluating unit: %s — Total changes: %d", unit->name, changes);
    return changes;
}


void wire_global_sources_to_destinations(Block *blk)
{
    LOG_INFO("🔁 Propagating values from Block.sources → Block.destinations...");

    for (size_t i = 0; i < blk->sources.count; ++i)
    {
        SourcePlace *src = blk->sources.items[i];
        if (!src || !src->resolved_name || !src->content)
            continue;

        for (size_t j = 0; j < blk->destinations.count; ++j)
        {
            DestinationPlace *dst = blk->destinations.items[j];
            if (!dst || !dst->resolved_name)
                continue;

            if (strcmp(src->resolved_name, dst->resolved_name) == 0)
            {
                LOG_INFO("🔗 Propagating %s → %s = %s", src->resolved_name, dst->resolved_name, src->content);

                free(dst->content); // clear any previous value
                dst->content = strdup(src->content);
            }
        }
    }
}

int eval(Block *blk)
{
    int total_changes = 0;
    int iteration = 0;

    LOG_INFO("🔁 Starting block evaluation loop (max %d iterations)...", MAX_ITERATIONS);

    do
    {
        LOG_INFO("🔂 Iteration %d", iteration + 1);

        int changes_this_round = 0;

        LOG_INFO("🔗 Wiring global sources → destinations before unit evaluation");
        wire_global_sources_to_destinations(blk);

        for (UnitList *node = blk->units; node != NULL; node = node->next)
        {
            Unit *unit = node->unit;
            int changed = eval_unit(unit, blk);
            changes_this_round += changed;
        }

        LOG_INFO("🔗 Wiring global sources → destinations after unit evaluation");
        wire_global_sources_to_destinations(blk);

        LOG_INFO("🔁 Iteration %d complete — Changes this round: %d", iteration + 1, changes_this_round);

        total_changes += changes_this_round;
        iteration++;

        if (changes_this_round == 0)
        {
            LOG_INFO("✅ Stabilization achieved — no further changes detected.");
            break;
        }

        if (iteration >= MAX_ITERATIONS)
        {
            LOG_WARN("⚠️ Maximum iteration count (%d) reached — possible infinite propagation loop or unstable logic.", MAX_ITERATIONS);
            break;
        }

    } while (1);

    LOG_INFO("🧮 Total signal changes across all iterations: %d", total_changes);
    dump_wiring(blk);
    return total_changes;
}
