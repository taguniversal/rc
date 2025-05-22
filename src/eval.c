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
  int idx = 0;

  DestinationPlace *def_dst = inv->definition->destinations;
  SourcePlace *inv_src = inv->sources;

  while (def_dst && inv_src)
  {
    LOG_INFO("   [%d] def_dst->resolved_name: %s", idx, def_dst->resolved_name ? def_dst->resolved_name : "(null)");
    LOG_INFO("   [%d] inv_src->resolved_name: %s", idx, inv_src->resolved_name ? inv_src->resolved_name : "(null)");
    LOG_INFO("   [%d] def_dst->content: %s", idx, def_dst->content ? def_dst->content : "(null)");
    LOG_INFO("   [%d] inv_src->content before: %s", idx, inv_src->content ? inv_src->content : "(null)");

    if (def_dst->content)
    {
      if (!inv_src->content || strcmp(inv_src->content, def_dst->content) != 0)
      {
        if (inv_src->content)
          free(inv_src->content);

        inv_src->content = strdup(def_dst->content);
        LOG_INFO("📤 [%d] Copied [%s] → Invocation source [%s]",
                 idx, def_dst->content, inv_src->resolved_name ? inv_src->resolved_name : "(null)");
        side_effects++;
      }
      else
      {
        LOG_INFO("🛑 [%d] Skipped copy: content already matches [%s]", idx, def_dst->content);
      }
    }
    else
    {
      LOG_WARN("⚠️ [%d] Definition destination [%s] has no content",
               idx, def_dst->resolved_name ? def_dst->resolved_name : "(null)");
    }

    // Copy to block-level destination (by def_dst name only, optional)
    DestinationPlace *outer_dst = find_destination(blk, def_dst->resolved_name);
    if (outer_dst)
    {
      if (!outer_dst->content || strcmp(outer_dst->content, def_dst->content) != 0)
      {
        if (outer_dst->content)
          free(outer_dst->content);
        outer_dst->content = def_dst->content ? strdup(def_dst->content) : NULL;
        LOG_INFO("🔗 [%d] Copied [%s] → Block-level destination [%s]",
                 idx, def_dst->content, outer_dst->resolved_name ? outer_dst->resolved_name : "(null)");
        side_effects++;
      }
    }

    def_dst = def_dst->next;
    inv_src = inv_src->next;
    idx++;
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
        if (def->destinations && def->place_of_resolution_sources)
        {
          for (DestinationPlace *def_dst = def->destinations; def_dst; def_dst = def_dst->next)
          {
            for (SourcePlace *por_src = def->place_of_resolution_sources; por_src; por_src = por_src->next)
            {
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
        SourcePlace *inv_src = inv->sources;
        DestinationPlace *def_dst = def->destinations;
        int i = 0;
        while (inv_src && def_dst)
        {
          if (def_dst->content)
          {
            if (!inv_src->content || strcmp(inv_src->content, def_dst->content) != 0)
            {
              side_effects = side_effects + propagate_content(inv_src, def_dst);
              LOG_INFO("🔌 [OUT] [%d] %s ← %s [%s]", i,
                       inv_src->resolved_name, def_dst->resolved_name, def_dst->content);
            }
          }
          inv_src = inv_src->next;
          def_dst = def_dst->next;
          i++;
        }

        break; // Stop once matched
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

  for (SourcePlace *src = blk->sources; src; src = src->next)
  {
    if (!src->resolved_name || !src->content)
      continue;

    for (DestinationPlace *dst = blk->destinations; dst; dst = dst->next)
    {
      if (!dst->resolved_name)
        continue;

      if (strcmp(src->resolved_name, dst->resolved_name) == 0)
      {
        side_effects = side_effects + propagate_content(src, dst);
      }
    }
  }

  return side_effects;
}

static void append_source(SourcePlace ***tail, SourcePlace *list)
{
  for (SourcePlace *s = list; s; s = s->next)
  {
    s->next_flat = NULL;
    **tail = s;
    *tail = &s->next_flat;
  }
}

static void append_one_source(SourcePlace ***tail, SourcePlace *s)
{
  s->next_flat = NULL;
  **tail = s;
  *tail = &s->next_flat;
}

static void append_dest(DestinationPlace ***tail, DestinationPlace *list)
{
  for (DestinationPlace *d = list; d; d = d->next)
  {
    d->next_flat = NULL;
    **tail = d;
    *tail = &d->next_flat;
  }
}

void flatten_signal_places(Block *blk)
{
  if (!blk)
    return;

  blk->sources = NULL;
  blk->destinations = NULL;

  SourcePlace **src_tail = &blk->sources;
  DestinationPlace **dst_tail = &blk->destinations;

  // Walk top-level invocations
  for (Invocation *inv = blk->invocations; inv; inv = inv->next)
  {
    append_source(&src_tail, inv->sources);
    append_dest(&dst_tail, inv->destinations);
  }

  // Walk definitions
  for (Definition *def = blk->definitions; def; def = def->next)
  {
    append_source(&src_tail, def->sources);
    append_dest(&dst_tail, def->destinations);

    // Place-of-resolution sources
    for (SourcePlace *por = def->place_of_resolution_sources; por; por = por->next)
    {
      append_one_source(&src_tail, por);
    }

    // Inline invocations inside the definition
    for (Invocation *inline_inv = def->invocations; inline_inv; inline_inv = inline_inv->next)
    {
      append_source(&src_tail, inline_inv->sources);
      append_dest(&dst_tail, inline_inv->destinations);
    }

    // POR invocations inside PlaceOfResolution (NEW)
    for (Invocation *por_inv = def->place_of_resolution_invocations; por_inv; por_inv = por_inv->next)
    {
      append_source(&src_tail, por_inv->sources);
      append_dest(&dst_tail, por_inv->destinations);
    }
  }

  // Ensure all flattened destinations are initialized
  for (DestinationPlace *dst = blk->destinations; dst; dst = dst->next_flat)
  {
    if (!dst->content)
    {
      dst->content = NULL; // Optional: strdup("") if you want to mark it non-null
      LOG_INFO("🧪 Initialized content for Destination: %s", dst->resolved_name);
    }
  }

  LOG_INFO("🧩 Flattened signal places: sources and destinations collected.");
}

DestinationPlace *find_output_by_resolved_name(const char *resolved_name, Definition *def)
{
  for (DestinationPlace *dst = def->destinations; dst != NULL; dst = dst->next)
  {
    if (dst->resolved_name && strcmp(dst->resolved_name, resolved_name) == 0)
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

    DestinationPlace *inv_dst = inv->destinations;
    SourcePlace *def_src = def->sources;

    int idx = 0;
    while (inv_dst && def_src)
    {
        if (inv_dst->content)
        {
            if (def_src->content)
                free(def_src->content);
            def_src->content = strdup(inv_dst->content);
            LOG_INFO("🔁 [IN] Transfer #%d: %s → %s [%s]",
                     idx,
                     inv_dst->resolved_name ? inv_dst->resolved_name : "(null)",
                     def_src->resolved_name ? def_src->resolved_name : "(null)",
                     def_src->content);
        }
        else
        {
            LOG_WARN("⚠️ [IN] Input %d: Invocation destination '%s' is empty",
                     idx, inv_dst->resolved_name ? inv_dst->resolved_name : "(null)");
        }

        inv_dst = inv_dst->next;
        def_src = def_src->next;
        idx++;
    }

    if (inv_dst || def_src)
    {
        LOG_WARN("⚠️ [IN] Mismatched input lengths during transfer (idx=%d)", idx);
    }
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
  if (!ci)
  {
    LOG_WARN("⚠️ Invocation '%s' has no ConditionalInvocation", inv->name);
    return 0;
  }

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

  for (SourcePlace *sp = inv->sources; sp; sp = sp->next)
  {
    if (!sp->name)
    {
      LOG_WARN("⚠️ Encountered unnamed SourcePlace, skipping...");
      continue;
    }

    if (sp->resolved_name && strcmp(sp->resolved_name, ci->resolved_output) == 0)
    {
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

  if (!def || !def->conditional_invocation)
  {
    LOG_WARN("⚠️ Definition '%s' has no conditional_invocation; skipping.", def ? def->name : "(null)");
    return 0;
  }

  if (!all_inputs_ready(def))
  {
    LOG_WARN("⚠️ Definition '%s' inputs not ready; skipping.", def->name);
    return 0;
  }

  LOG_INFO("🔬 Preparing evaluation for definition: %s", def->name);

  for (SourcePlace *src = def->sources; src; src = src->next)
  {
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

  // 1. Write result to named output
  int output_status = write_result_to_named_output(def, def->conditional_invocation->resolved_output, result);
  if (output_status < 0)
    return 0;
  side_effects += output_status;

  // 2. Sync internal SourcePlace(s) that have the same name as the output
  for (SourcePlace *sp = def->sources; sp; sp = sp->next)
  {
    if (sp->name && strcmp(sp->name, def->conditional_invocation->resolved_output) == 0)
    {
      if (sp->content)
        free(sp->content);
      sp->content = strdup(result);
      side_effects++;
      LOG_INFO("🔁 Synced SourcePlace '%s' with content [%s]", sp->name, sp->content);
    }
  }

  // 2.5. Sync Place-of-Resolution sources too
  for (SourcePlace *sp = def->place_of_resolution_sources; sp; sp = sp->next)
  {
    if (sp->name && strcmp(sp->name, def->conditional_invocation->resolved_output) == 0)
    {
      if (sp->content)
        free(sp->content);
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

  // 3. Propagate to invocations watching this signal
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

    // 2. Evaluate definitions
    for (Definition *def = blk->definitions; def; def = def->next)
    {
      side_effect_this_round += eval_definition(def, blk);
    }

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
    LOG_WARN("⚠️ Eval for %s did not stabilize after %d iterations. Bailout "
             "triggered.",
             blk->psi, MAX_ITERATIONS);
  }

  return total_side_effects;
}

void dump_signals(Block *blk)
{
  LOG_INFO("🔍 Dumping signals for Block: %s", blk->psi);
  int count = 0;

  for (SourcePlace *src = blk->sources; src != NULL; src = src->next_flat)
  {
    const char *label = src->resolved_name ? src->resolved_name : (src->name ? src->name : "<unnamed>");
    const char *content = (src->content)
                              ? src->content
                              : "<null>";
    LOG_INFO("📡 [%d] %s → %s", ++count, label, content);
  }

  LOG_INFO("🔍 Total signals dumped: %d", count);
}
