#include "log.h"
#include "sqlite3.h"
#include "util.h"
#include "wiring.h"
#include "eval_util.h"
#include "eval.h"
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

int count_invocations(Definition *def)
{
  int count = 0;
  for (Invocation *inv = def->invocations; inv; inv = inv->next)
    count++;
  return count;
}

int pull_outputs_from_definition(Block *blk, Invocation *inv)
{
  if (!inv || !inv->definition)
  {
    LOG_WARN("⚠️ pull_outputs_from_definition - either inv or inv->definition NULL");
    return 0;
  }

  LOG_INFO("🧩 Pulling outputs for Invocation '%s'", inv->name);

  int side_effects = 0;
  size_t def_count = inv->definition->destinations.count;
  size_t inv_count = inv->sources.count;
  size_t limit = def_count < inv_count ? def_count : inv_count;

  for (size_t idx = 0; idx < limit; ++idx)
  {
    DestinationPlace *def_dst = inv->definition->destinations.items[idx];
    SourcePlace *inv_src = inv->sources.items[idx];

    LOG_INFO("   [%zu] def_dst->resolved_name: %s", idx, def_dst->resolved_name ? def_dst->resolved_name : "(null)");
    LOG_INFO("   [%zu] inv_src->resolved_name: %s", idx, inv_src->resolved_name ? inv_src->resolved_name : "(null)");
    LOG_INFO("   [%zu] def_dst->content: %s", idx, def_dst->content ? def_dst->content : "(null)");
    LOG_INFO("   [%zu] inv_src->content before: %s", idx, inv_src->content ? inv_src->content : "(null)");

    if (def_dst->content)
    {
      if (!inv_src->content || strcmp(inv_src->content, def_dst->content) != 0)
      {
        if (inv_src->content)
          free(inv_src->content);

        inv_src->content = strdup(def_dst->content);
        LOG_INFO("📤 [%zu] Copied [%s] → Invocation source [%s]",
                 idx, def_dst->content, inv_src->resolved_name ? inv_src->resolved_name : "(null)");
        side_effects++;
      }
      else
      {
        LOG_INFO("🛑 [%zu] Skipped copy: content already matches [%s]", idx, def_dst->content);
      }
    }
    else
    {
      LOG_WARN("⚠️ [%zu] Definition destination [%s] has no content",
               idx, def_dst->resolved_name ? def_dst->resolved_name : "(null)");
    }

    DestinationPlace *outer_dst = find_destination(blk, def_dst->resolved_name);
    if (outer_dst)
    {
      if (!outer_dst->content || strcmp(outer_dst->content, def_dst->content) != 0)
      {
        if (outer_dst->content)
          free(outer_dst->content);
        outer_dst->content = def_dst->content ? strdup(def_dst->content) : NULL;
        LOG_INFO("🔗 [%zu] Copied [%s] → Block-level destination [%s]",
                 idx, def_dst->content, outer_dst->resolved_name ? outer_dst->resolved_name : "(null)");
        side_effects++;
      }
    }
  }

  return side_effects;
}

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
                if (def->destinations.count > 0 && def->place_of_resolution_sources.count > 0)
                {
                    for (size_t d = 0; d < def->destinations.count; ++d)
                    {
                        DestinationPlace *def_dst = def->destinations.items[d];
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
                size_t count = MIN(inv->sources.count, def->destinations.count);
                for (size_t i = 0; i < count; ++i)
                {
                    SourcePlace *inv_src = inv->sources.items[i];
                    DestinationPlace *def_dst = def->destinations.items[i];

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


int wire_signal_propagation_by_name(Block *blk)
{
    int side_effects = 0;

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
                side_effects += propagate_content(src, dst);
            }
        }
    }

    return side_effects;
}

void flatten_signal_places(Block *blk)
{
    if (!blk)
        return;

    blk->sources.count = 0;
    blk->sources.items = NULL;
    blk->destinations.count = 0;
    blk->destinations.items = NULL;

    LOG_INFO("🔍 Beginning signal flattening...");

    // Helper macros to add source/dest one-by-one
    #define ADD_SRC(sp) do { if (sp) append_source(&blk->sources, sp); } while(0)
    #define ADD_DST(dp) do { if (dp) append_destination(&blk->destinations, dp); } while(0)

    // Top-level invocations
    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
        for (size_t i = 0; i < inv->sources.count; ++i)
        {
            SourcePlace *sp = inv->sources.items[i];
            LOG_INFO("➕ Source (top-level invocation): %s", sp->resolved_name);
            ADD_SRC(sp);
        }

        for (size_t i = 0; i < inv->destinations.count; ++i)
        {
            DestinationPlace *dp = inv->destinations.items[i];
            LOG_INFO("➕ Dest (top-level invocation): %s", dp->resolved_name);
            ADD_DST(dp);
        }
    }

    // Definitions
    for (Definition *def = blk->definitions; def; def = def->next)
    {
        for (size_t i = 0; i < def->sources.count; ++i)
        {
            SourcePlace *sp = def->sources.items[i];
            LOG_INFO("➕ Source (definition): %s", sp->resolved_name);
            ADD_SRC(sp);
        }

        for (size_t i = 0; i < def->destinations.count; ++i)
        {
            DestinationPlace *dp = def->destinations.items[i];
            LOG_INFO("➕ Dest (definition): %s", dp->resolved_name);
            ADD_DST(dp);
        }

        for (size_t i = 0; i < def->place_of_resolution_sources.count; ++i)
        {
            SourcePlace *por = def->place_of_resolution_sources.items[i];
            LOG_INFO("➕ Source (POR): %s", por->resolved_name);
            ADD_SRC(por);
        }

        for (Invocation *inline_inv = def->invocations; inline_inv; inline_inv = inline_inv->next)
        {
            for (size_t i = 0; i < inline_inv->sources.count; ++i)
            {
                SourcePlace *sp = inline_inv->sources.items[i];
                LOG_INFO("➕ Source (inline invocation): %s", sp->resolved_name);
                ADD_SRC(sp);
            }

            for (size_t i = 0; i < inline_inv->destinations.count; ++i)
            {
                DestinationPlace *dp = inline_inv->destinations.items[i];
                LOG_INFO("➕ Dest (inline invocation): %s", dp->resolved_name);
                ADD_DST(dp);
            }
        }

        for (Invocation *por_inv = def->place_of_resolution_invocations; por_inv; por_inv = por_inv->next)
        {
            for (size_t i = 0; i < por_inv->sources.count; ++i)
            {
                SourcePlace *sp = por_inv->sources.items[i];
                LOG_INFO("➕ Source (POR invocation): %s", sp->resolved_name);
                ADD_SRC(sp);
            }

            for (size_t i = 0; i < por_inv->destinations.count; ++i)
            {
                DestinationPlace *dp = por_inv->destinations.items[i];
                LOG_INFO("➕ Dest (POR invocation): %s", dp->resolved_name);
                ADD_DST(dp);
            }
        }
    }

    // Print final
    LOG_INFO("🧾 Final Flattened Source List:");
    for (size_t i = 0; i < blk->sources.count; ++i)
    {
        SourcePlace *sp = blk->sources.items[i];
        LOG_INFO("   📡 Source: %s [%s]", sp->resolved_name, sp->content ? sp->content : "null");
    }

    LOG_INFO("🧾 Final Flattened Destination List:");
    for (size_t i = 0; i < blk->destinations.count; ++i)
    {
        DestinationPlace *dp = blk->destinations.items[i];
        LOG_INFO("   🎯 Dest:   %s [%s]", dp->resolved_name, dp->content ? dp->content : "null");
    }

    #undef ADD_SRC
    #undef ADD_DST
}

DestinationPlace *find_output_by_resolved_name(const char *resolved_name, Definition *def)
{
    if (!resolved_name || !def)
        return NULL;

    for (size_t i = 0; i < def->destinations.count; ++i)
    {
        DestinationPlace *dst = def->destinations.items[i];
        if (dst && dst->resolved_name && strcmp(dst->resolved_name, resolved_name) == 0)
        {
            return dst;
        }
    }

    LOG_WARN("⚠️ No destination found matching resolved name: %s in def: %s", resolved_name, def->name);
    return NULL;
}
void transfer_invocation_inputs_to_definition(Invocation *inv, Definition *def)
{
  if (!inv || !def)
    return;

  size_t count = (inv->destinations.count < def->sources.count)
                   ? inv->destinations.count
                   : def->sources.count;

  for (size_t idx = 0; idx < count; ++idx)
  {
    DestinationPlace *inv_dst = inv->destinations.items[idx];
    SourcePlace *def_src = def->sources.items[idx];

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

  if (inv->destinations.count != def->sources.count)
  {
    LOG_WARN("⚠️ [IN] Mismatched input lengths during transfer: invocation.destinations=%zu, def.sources=%zu",
             inv->destinations.count, def->sources.count);
  }
}


int evaluate_definition_invocations_until_stable(Definition *def, Block *blk)
{
  if (!def)
  {
    LOG_WARN("⚠️ Null Definition — skipping internal invocation evaluation.");
    return 0;
  }

  LOG_INFO("🔁 Definition '%s' has no ConditionalInvocation — evaluating internal invocations until stable...", def->name);

  int side_effects = 0;
  int round = 0;

  while (round++ < 5)
  {
    int changed = 0;

    // 1. Evaluate all invocations with ready inputs
    for (Invocation *inv = def->place_of_resolution_invocations; inv; inv = inv->next)
    {
      LOG_INFO("🔍 POR Eval Check: %s has definition? %s", inv->name, inv->definition ? "YES" : "NO");
      if (inv->definition && all_inputs_ready(inv->definition))
      {
        LOG_INFO("🚀 Evaluating POR Invocation: %s (inputs ready)", inv->name);
        changed += eval_invocation(inv, blk);
      }
      else
      {
        LOG_INFO("⏭️ Skipping POR Invocation: %s (inputs not ready)", inv->name);
      }
    }

    // 2. Wire POR→POR and POR→Definition destinations
    changed += propagate_por_signals(def);
    // ✅ 3. Propagate POR outputs to definition destinations
    changed += propagate_por_outputs_to_definition(def);
    // 4. Sync to outer context 
    changed += sync_invocation_outputs_to_definition(def);

    if (changed == 0)
    {
      LOG_INFO("✅ Definition '%s' stabilized after %d rounds", def->name, round);
      break;
    }

    side_effects += changed;
    LOG_INFO("🔁 Definition '%s' iteration %d changed signals: %d", def->name, round, changed);
  }

  if (round >= 5)
  {
    LOG_WARN("⚠️ Definition '%s' did not stabilize after %d rounds", def->name, round);
  }

  return side_effects;
}


int eval_invocation(Invocation *inv, Block *blk)
{
    if (!inv)
    {
        LOG_WARN("⚠️ Null Invocation passed to eval_invocation, skipping.");
        return 0;
    }

    if (!inv->definition)
    {
        LOG_WARN("⚠️ Invocation %s has no linked Definition — skipping", inv->name);
        return 0;
    }

    LOG_INFO("🚀 Evaluating Invocation: %s", inv->name);
    LOG_INFO("📥 Copying inputs for invocation: %s", inv->name);
    transfer_invocation_inputs_to_definition(inv, inv->definition);

    ConditionalInvocation *ci = inv->definition->conditional_invocation;

    // === Path A: POR-style internal evaluation
    if (!ci)
    {
        if (inv->definition->place_of_resolution_invocations)
        {
            LOG_INFO("↪️ Invocation '%s' has no ConditionalInvocation — assuming POR internal definition", inv->name);
            return evaluate_definition_invocations_until_stable(inv->definition, blk);
        }
        else
        {
            LOG_WARN("⚠️ Invocation '%s' has no ConditionalInvocation and no internal invocations — skipping", inv->name);
            return 0;
        }
    }

    // === Path B: Evaluate using conditional logic
    LOG_INFO("🔍 Invocation '%s' ConditionalInvocation: arg_count=%d", inv->name, ci->arg_count);

    if (ci->arg_count > 16)
    {
        LOG_ERROR("❌ Invocation '%s' has suspicious arg_count=%d", inv->name, ci->arg_count);
        return 0;
    }

    if (!ci->resolved_template_args || ci->arg_count == 0)
    {
        LOG_WARN("⚠️ ConditionalInvocation for '%s' has no resolved_template_args or zero arg_count", inv->name);
        return 0;
    }

    LOG_INFO("🔧 About to build pattern for Invocation '%s'", inv->name);

    char pattern[128];
    if (!build_input_pattern(inv->definition, ci->resolved_template_args, ci->arg_count, pattern, sizeof(pattern)))
    {
        LOG_WARN("❌ Failed to build input pattern for Invocation: %s", inv->name);
        return 0;
    }

    LOG_INFO("🔎 Built input pattern for '%s': %s", inv->name, pattern);

    const char *result = match_conditional_case(ci, pattern);
    if (!result)
    {
        LOG_WARN("❌ No matching case for pattern: %s", pattern);
        return 0;
    }

    LOG_INFO("📤 Matched result for '%s': %s", inv->name, result);

    int side_effects = 0;
    int output_status = write_result_to_named_output(inv->definition, ci->resolved_output, result);
    if (output_status < 0)
    {
        LOG_ERROR("❌ Failed to write result to named output '%s'", ci->resolved_output);
        return 0;
    }

    side_effects += output_status;

    DestinationPlace *def_dst = find_output_by_resolved_name(ci->resolved_output, inv->definition);
    if (!def_dst || !def_dst->content)
    {
        LOG_ERROR("❌ Output destination '%s' has no content", ci->resolved_output);
        return side_effects;
    }

    for (size_t i = 0; i < inv->sources.count; ++i)
    {
        SourcePlace *sp = inv->sources.items[i];
        if (!sp || !sp->name)
        {
            LOG_WARN("⚠️ Encountered unnamed SourcePlace, skipping...");
            continue;
        }

        if (sp->resolved_name && strcmp(sp->resolved_name, ci->resolved_output) == 0)
        {
            if (sp->content)
                free(sp->content);
            sp->content = strdup(def_dst->content);
            LOG_INFO("🔁 Bound Invocation SourcePlace '%s' to result [%s]", sp->name, sp->content);
        }
        else
        {
            LOG_INFO("🛑 SourcePlace '%s' does not match output '%s'", sp->resolved_name, ci->resolved_output);
        }

        if (sp->content)
        {
            LOG_INFO("✅ Signal content after eval: %s → %s", sp->name, sp->content);
        }
        else
        {
            LOG_WARN("⚠️ Signal content unavailable after eval for SourcePlace '%s'", sp->name);
        }
    }

    return side_effects;
}

int eval_definition(Definition *def, Block *blk)
{
    int side_effects = 0;

    if (!def)
    {
        LOG_WARN("⚠️ Null Definition — skipping.");
        return 0;
    }

    if (!def->conditional_invocation)
    {
        LOG_INFO("🔁 Definition '%s' has no ConditionalInvocation — evaluating internal invocations until stable...", def->name);
        side_effects += evaluate_definition_invocations_until_stable(def, blk);
        return side_effects;
    }

    if (!all_inputs_ready(def))
    {
        LOG_WARN("⚠️ Definition '%s' inputs not ready; skipping.", def->name);
        return 0;
    }

    LOG_INFO("🔬 Preparing evaluation for definition: %s", def->name);

    for (size_t i = 0; i < def->sources.count; ++i)
    {
        SourcePlace *src = def->sources.items[i];
        if (!src) continue;

        if (src->resolved_name)
            LOG_INFO("🛰️ SourcePlace name: %s", src->resolved_name);
        else if (src->content)
            LOG_INFO("💎 SourcePlace literal: %s", src->content);
        else
            LOG_WARN("❓ SourcePlace unnamed and empty?");
    }

    char pattern[128];
    if (!build_input_pattern(def,
                             def->conditional_invocation->resolved_template_args,
                             def->conditional_invocation->arg_count,
                             pattern, sizeof(pattern)))
    {
        LOG_ERROR("❌ Failed to build input pattern for definition: %s", def->name);
        return 0;
    }

    LOG_INFO("🔎 Evaluating definition %s with input pattern [%s]", def->name, pattern);

    const char *result = match_conditional_case(def->conditional_invocation, pattern);
    if (!result)
    {
        LOG_WARN("⚠️ No pattern matched in %s for input [%s]", def->name, pattern);
        return 0;
    }

    LOG_INFO("📦 Writing result to output signal: %s (resolved as: %s)",
             def->conditional_invocation->resolved_output,
             def->conditional_invocation->resolved_output ? def->conditional_invocation->resolved_output : "(null)");

    int output_status = write_result_to_named_output(def, def->conditional_invocation->resolved_output, result);
    if (output_status < 0)
        return 0;
    side_effects += output_status;

    // 2. Sync internal SourcePlaces
    for (size_t i = 0; i < def->sources.count; ++i)
    {
        SourcePlace *sp = def->sources.items[i];
        if (!sp || !sp->name) continue;

        if (strcmp(sp->name, def->conditional_invocation->resolved_output) == 0)
        {
            if (sp->content) free(sp->content);
            sp->content = strdup(result);
            side_effects++;
            LOG_INFO("🔁 Synced SourcePlace '%s' with content [%s]", sp->name, sp->content);
        }
    }

    // 2.5. Sync Place-of-Resolution sources too
    for (size_t i = 0; i < def->place_of_resolution_sources.count; ++i)
    {
        SourcePlace *sp = def->place_of_resolution_sources.items[i];
        if (!sp || !sp->name) continue;

        if (strcmp(sp->name, def->conditional_invocation->resolved_output) == 0)
        {
            if (sp->content) free(sp->content);
            sp->content = strdup(result);
            side_effects++;
            LOG_INFO("🔁 Synced POR SourcePlace '%s' with content [%s]", sp->name, sp->content);
        }
    }

    DestinationPlace *dst = find_output_by_resolved_name(def->conditional_invocation->resolved_output, def);
    if (!dst || !dst->content)
    {
        LOG_ERROR("❌ Destination written but content is missing: %s", def->conditional_invocation->resolved_output);
        return side_effects;
    }

    side_effects += propagate_output_to_invocations(blk, dst, dst->content);
    return side_effects;
}


int eval(Block *blk)
{
  LOG_INFO("⚙️  Starting eval() pass for %s (until stabilization)", blk->psi);
  int total_side_effects = 0;
  int iteration = 0;

  while (iteration < MAX_ITERATIONS)
  {
    int side_effect_this_round = 0;
    iteration++;

    // 1. Inject inputs into definitions
    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
      side_effect_this_round += eval_invocation(inv, blk);
    }

    // 2. Evaluate definitions and wire POR invocations inside each
    for (Definition *def = blk->definitions; def; def = def->next)
    {
      side_effect_this_round += eval_definition(def, blk);
      side_effect_this_round += propagate_por_signals(def); // 🔧 NEW
    }

    side_effect_this_round += wire_signal_propagation_by_name(blk);

    // 3. Pull outputs back into invocations
    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
      side_effect_this_round += pull_outputs_from_definition(blk, inv);
    }

    side_effect_this_round += propagate_intrablock_signals(blk);

    total_side_effects += side_effect_this_round;

    if (side_effect_this_round == 0)
    {
      LOG_INFO("✅ Stabilization reached after %d iterations.", iteration);
      break;
    }
  }

  if (iteration >= MAX_ITERATIONS)
  {
    LOG_WARN("⚠️ Eval for %s did not stabilize after %d iterations. Bailout triggered.", blk->psi, MAX_ITERATIONS);
  }

  return total_side_effects;
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
