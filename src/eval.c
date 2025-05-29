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

int eval(Block *blk);
int eval_definition(Definition *def, Block *blk);
int eval_invocation(Invocation *inv, Block *blk);
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
            total_len += 1;  // Use "?" placeholder if missing
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

int eval_unit(Unit *unit, Block *blk)
{
    int change_count = 0;

    Definition *def = unit->definition;
    Invocation *inv = unit->invocation;

    if (!def || !inv)
    {
        LOG_WARN("⚠️ Unit missing definition or invocation: %s", unit->name);
        return 0;
    }

    LOG_INFO("🔍 Evaluating unit: %s", unit->name);

    // Step 1: Propagate inputs from invocation → definition
    transfer_invocation_inputs_to_definition(inv, def);

    // Step 2: Evaluate ConditionalInvocation if present
    if (def->conditional_invocation)
    {
        LOG_INFO("🔘 Running conditional logic for unit: %s", unit->name);

        const char *pattern = build_conditional_pattern(def->conditional_invocation, blk);
        if (!pattern)
        {
            LOG_ERROR("❌ Failed to build pattern for ConditionalInvocation.");
            return change_count;
        }

        LOG_INFO("🧩 Built pattern: %s", pattern);

        const char *result = match_conditional_case(def->conditional_invocation, pattern);
        free((void *)pattern);

        if (!result)
        {
            LOG_WARN("⚠️ No match found for ConditionalInvocation pattern.");
        }
        else
        {
            LOG_INFO("📤 Match result: %s", result);

            const char *output_name = def->conditional_invocation->resolved_output;
            DestinationPlace *dst = find_destination(blk, output_name);
            if (!dst)
            {
                LOG_ERROR("❌ Failed to find DestinationPlace for output name: %s", output_name);
            }
            else
            {
                if (dst->content)
                    free(dst->content);
                dst->content = strdup(result);
                LOG_INFO("✅ Wrote result to output: %s = %s", output_name, dst->content);
                change_count++;
            }

            // Also bind to invocation’s boundary_sources if any match
            for (size_t i = 0; i < inv->boundary_sources.count; ++i)
            {
                SourcePlace *sp = inv->boundary_sources.items[i];
                if (!sp || !sp->resolved_name)
                    continue;
                if (strcmp(sp->resolved_name, output_name) == 0)
                {
                    if (sp->content)
                        free(sp->content);
                    sp->content = strdup(result);
                    LOG_INFO("🔁 Bound invocation output: %s = %s", sp->resolved_name, sp->content);
                    change_count++;
                }
            }
        }
    }

    // Step 3: Propagate internal signals between invocations inside definition
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
                        change_count += propagate_content(src, dst);
                    }
                }
            }
        }
    }

    // Step 4: Propagate outputs from unit → block
    for (size_t i = 0; i < inv->boundary_sources.count; ++i)
    {
        SourcePlace *src = inv->boundary_sources.items[i];
        if (!src || !src->resolved_name || !src->content)
            continue;

        DestinationPlace *dst = find_destination(blk, src->resolved_name);
        if (dst)
        {
            change_count += propagate_content(src, dst);
        }
    }

    // Step 5: Propagate inputs from block → unit
    for (size_t i = 0; i < inv->boundary_destinations.count; ++i)
    {
        DestinationPlace *dst = inv->boundary_destinations.items[i];
        if (!dst || !dst->resolved_name)
            continue;

        SourcePlace *src = find_source(blk, dst->resolved_name);
        if (src && src->content)
        {
            change_count += propagate_content(src, dst);
        }
    }

    return change_count;
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

    wire_global_sources_to_destinations(blk);

    for (UnitList *node = blk->units; node != NULL; node = node->next)
    {
        Unit *unit = node->unit;
        int changed = eval_unit(unit, blk);  // <- blk passed for global signal access
        total_changes += changed;
    }

    wire_global_sources_to_destinations(blk);

    return total_changes;
}


void dump_signals(Block *blk)
{
    LOG_INFO("🔍 Dumping signals for Block: %s", blk->psi);
    int count = 0;

    for (size_t i = 0; i < blk->sources.count; ++i)
    {
        SourcePlace *src = blk->sources.items[i];
        if (!src) continue;

        const char *label = src->resolved_name ? src->resolved_name : (src->name ? src->name : "<unnamed>");
        const char *content = src->content ? src->content : "<null>";
        LOG_INFO("📡 [%d] %s → %s", ++count, label, content);
    }

    LOG_INFO("🔍 Total signals dumped: %d", count);
}
