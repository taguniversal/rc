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

struct Signal NULL_SIGNAL = {
    .content = NULL,
    .next = NULL,
#ifdef SAFETY_GUARD
    .safety_guard = 0xDEADBEEFCAFEBABE,
#endif
};

int eval(Block *blk);
int eval_definition(Definition *def, Block *blk, int* side_effects);
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
    return 0;

  int side_effects = 0;

  DestinationPlace *inv_dst = inv->destinations;
  DestinationPlace *def_dst = inv->definition->destinations;

  while (inv_dst && def_dst)
  {
    if (inv_dst->signal == &NULL_SIGNAL && def_dst->signal && def_dst->signal->content)
    {
      Signal *new_sig = (Signal *)calloc(1, sizeof(Signal));
      if (!new_sig)
      {
        LOG_ERROR("❌ Memory allocation failed while copying Definition output");
        continue;
      }

      new_sig->content = strdup(def_dst->signal->content);
      new_sig->next = NULL;
      inv_dst->signal = new_sig;

      LOG_INFO("📤 Pulled output [%s] into Invocation destination [%s]",
               def_dst->signal->content, inv_dst->resolved_name);
      side_effects++;
    }

    DestinationPlace *outer_dst = find_destination(blk, inv_dst->resolved_name);
    if (outer_dst && (outer_dst->signal == &NULL_SIGNAL))
    {
      outer_dst->signal = inv_dst->signal;
      LOG_INFO("🔗 Propagated Invocation Destination [%s] outward", inv_dst->resolved_name);
    }

    inv_dst = inv_dst->next;
    def_dst = def_dst->next;
  }

  return side_effects;
}

void wire_output(SourcePlace *src, DestinationPlace *dst)
{
  src->signal = dst->signal;
  LOG_INFO("Wired SourcePlace '%s' to DestinationPlace '%s' → %p", src->resolved_name, dst->resolved_name, dst->signal);
}

void wire_by_name_correspondence(Block *blk)
{
  if (!blk)
    return;

  for (Definition *def = blk->definitions; def; def = def->next)
  {
    // For each DestinationPlace in this definition...
    for (DestinationPlace *dst = def->destinations; dst; dst = dst->next)
    {
      bool matched = false;

      // Look for a SourcePlace with the same name
      for (SourcePlace *src = def->sources; src; src = src->next)
      {
        if (strcmp(dst->resolved_name, src->resolved_name) == 0)
        {
          dst->signal = src->signal;
          matched = true;
          LOG_INFO("🔁 Name-wire: Destination '%s' gets signal from Source '%s' → (0x%p)", dst->resolved_name, src->resolved_name, src->signal);
          break;
        }
      }

      if (!matched)
      {
        LOG_WARN("⚠️ Name-wire: No source found matching destination '%s' in definition '%s'", dst->resolved_name, def->name);
      }
    }
  }
}

void link_invocations_by_position(Block *blk)
{
  for (Invocation *inv = blk->invocations; inv; inv = inv->next)
  {
    for (Definition *def = blk->definitions; def; def = def->next)
    {
      if (strcmp(inv->name, def->name) == 0)
      {
        inv->definition = def;
        LOG_INFO("🔗 Linked Invocation %s → Definition %s", inv->name, def->name);

        // Wire outputs (Invocation SourcePlaces ← Definition Destinations)
        SourcePlace *inv_src = inv->sources;
        DestinationPlace *def_dst = def->destinations;
        int i = 0;
        while (inv_src && def_dst)
        {
          inv_src->signal = def_dst->signal;
          LOG_INFO("🔌 [OUT] [%d] %s ← %s (0x%p)", i, inv_src->resolved_name, def_dst->resolved_name, def_dst->signal);
          inv_src = inv_src->next;
          def_dst = def_dst->next;
          i++;
        }

        // Wire inputs (Invocation Destinations → Definition Sources)
        DestinationPlace *inv_dst = inv->destinations;
        SourcePlace *def_src = def->sources;
        i = 0;
        while (inv_dst && def_src)
        {
          def_src->signal = inv_dst->signal;
          LOG_INFO("🔌 [IN]  [%d] %s ← %s (0x%p)", i, def_src->resolved_name, inv_dst->resolved_name, inv_dst->signal);
          inv_dst = inv_dst->next;
          def_src = def_src->next;
          i++;
        }

        break;
      }
    }

    if (!inv->definition)
    {
      LOG_WARN("⚠️ No matching Definition found for Invocation %s", inv->name);
    }
  }
}

void wire_signal_propagation_by_name(Block *blk)
{
  for (SourcePlace *src = blk->sources; src; src = src->next)
  {
    for (DestinationPlace *dst = blk->destinations; dst; dst = dst->next)
    {
      if (strcmp(src->resolved_name, dst->resolved_name) == 0)
      {
        dst->signal = src->signal;
        LOG_INFO("📡 Signal propagation: %s → %s (0x%p)", src->resolved_name, dst->resolved_name, dst->signal);
      }
    }
  }
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

  // Walk invocations
  for (Invocation *inv = blk->invocations; inv; inv = inv->next)
  {
    append_source(&src_tail, inv->sources);
    append_dest(&dst_tail, inv->destinations);
  }

  // Walk definitions + inline invocations
  for (Definition *def = blk->definitions; def; def = def->next)
  {
    append_source(&src_tail, def->sources);
    append_dest(&dst_tail, def->destinations);

    for (Invocation *inline_inv = def->invocations; inline_inv; inline_inv = inline_inv->next)
    {
      append_source(&src_tail, inline_inv->sources);
      append_dest(&dst_tail, inline_inv->destinations);
    }
  }

  LOG_INFO("🧩 Flattened signal places: sources and destinations collected.");
}

void print_signal_places(Block *blk)
{
  LOG_INFO("🔍 Flattened Signal Places:");
  for (SourcePlace *src = blk->sources; src; src = src->next_flat)
  {
    LOG_INFO("Source: %s (0x%p)", src->resolved_name, src->signal);
  }
  for (DestinationPlace *dst = blk->destinations; dst; dst = dst->next_flat)
  {
    LOG_INFO("Destination: %s (0x%p)", dst->resolved_name, dst->signal);
  }
}

int eval_invocation(Invocation *inv, Block *blk)
{
  (void)blk;
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

  int side_effects = copy_invocation_inputs(inv);

  ConditionalInvocation *ci = inv->definition->conditional_invocation;
  if (!ci)
    return side_effects;

  char pattern[128];
  if (!build_input_pattern(inv->definition, ci->resolved_template_args, ci->arg_count, pattern, sizeof(pattern)))
  {
    LOG_WARN("❌ Failed to build input pattern for Invocation: %s", inv->name);
    return side_effects;
  }

  LOG_INFO("🔎 Built input pattern for '%s': %s", inv->name, pattern);

  const char *result = match_conditional_case(ci, pattern);
  if (!result)
  {
    LOG_WARN("❌ No matching case for pattern: %s", pattern);
    return side_effects;
  }

  int output_index = write_result_to_named_output(inv->definition, ci->resolved_output, result, &side_effects);
  if (output_index < 0)
    return side_effects;


  // Step 2: Copy result to corresponding Invocation SourcePlace by position
  SourcePlace *src_in_inv = inv->sources;
  size_t i = 0;
  while (src_in_inv && i < output_index)
  {
    src_in_inv = src_in_inv->next;
    i++;
  }

  if (src_in_inv && (!src_in_inv->signal || src_in_inv->signal == &NULL_SIGNAL))
  {
    Signal *sig = (Signal *)calloc(1, sizeof(Signal));
    sig->content = strdup(result);
    sig->next = NULL;
    src_in_inv->signal = sig;
  }

  return side_effects;
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

int eval_definition(Definition *def, Block *blk, int* side_effects)
{

  if (!def || !def->conditional_invocation)
    return 0;
  if (!all_inputs_ready(def))
    return 0;

  LOG_INFO("🔬 Preparing evaluation for definition: %s", def->name);

  for (SourcePlace *src = def->sources; src; src = src->next)
  {
    if (src->resolved_name)
      LOG_INFO("🛰️ SourcePlace name: %s", src->resolved_name);
    else if (src->signal && src->signal->content)
      LOG_INFO("💎 SourcePlace literal: %s", src->signal->content);
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

  DestinationPlace *dst = find_output_by_resolved_name(def->conditional_invocation->resolved_output, def);
  if (dst && dst->signal == &NULL_SIGNAL)
  {
    int output_index = write_result_to_named_output(def, def->conditional_invocation->resolved_output, result, &side_effects);
    if (output_index < 0)
      return 0;

    DestinationPlace *dst = find_output_by_resolved_name(def->conditional_invocation->resolved_output, def);
    if (!dst || !dst->signal)
    {
      LOG_ERROR("❌ Destination written but signal not found: %s", def->conditional_invocation->resolved_output);
      return 0;
    }

    LOG_INFO("✍️ Output written: %s → [%s]", dst->resolved_name, dst->signal->content);
    propagate_output_to_invocations(blk, dst, dst->signal);
  }
  else
  {
    LOG_WARN("⚠️ No destination ready for output in %s", def->name);
    return 0;
  }
  return 0;
}

void allocate_signals(Block *blk)
{
  LOG_INFO("🔧 Allocating signals for destination places...");

  for (Invocation *inv = blk->invocations; inv != NULL; inv = inv->next)
  {
    for (DestinationPlace *dst = inv->destinations; dst != NULL; dst = dst->next)
    {
      if (dst->signal == &NULL_SIGNAL)
      {
        dst->signal = calloc(1, sizeof(Signal));
        if (!dst->signal)
        {
          LOG_ERROR("❌ Failed to allocate signal for destination '%s' in '%s'", dst->resolved_name, inv->name);
          exit(1);
        }
        dst->signal->content = NULL; // Or set default
        LOG_INFO("✅ Allocated Signal %p for Destination '%s' in Invocation '%s'",
                 (void *)dst->signal, dst->resolved_name, inv->name);
      }
    }
  }

  LOG_INFO("✅ Signal allocation complete.");
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
      int effects = eval_invocation(inv, blk);
      side_effect_this_round += effects;
    }

    // 2. Evaluate definitions
    for (Definition *def = blk->definitions; def; def = def->next)
    {
      int effects = eval_definition(def, blk, &side_effect_this_round);
      side_effect_this_round += effects;
    }

    // 3. Pull outputs back into invocations
    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
      int effects = pull_outputs_from_definition(blk, inv);
      side_effect_this_round += effects;
    }

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
    const char *content = (src->signal && src->signal->content)
                              ? src->signal->content
                              : "<null>";
    LOG_INFO("📡 [%d] %s → %s", ++count, label, content);
  }

  LOG_INFO("🔍 Total signals dumped: %d", count);
}
