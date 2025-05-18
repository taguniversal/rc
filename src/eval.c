#include "log.h"
#include "sqlite3.h"
#include "util.h"
#include "wiring.h"
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>  // fprintf, stderr
#include <stdlib.h> // general utilities (optional but common)
#include <string.h>
#include <time.h>
#include <time.h>        // time_t, localtime, strftime
#include <unistd.h>      // isatty, fileno
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
int eval_definition(Definition *def, Block *blk);
int eval_invocation(Invocation *inv, Block *blk);
int pull_outputs_from_definition(Block *blk, Invocation *inv);
static DestinationPlace *find_destination(Block *blk, const char *name);
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

static DestinationPlace *find_destination(Block *blk, const char *name)
{
  for (Invocation *inv = blk->invocations; inv; inv = inv->next)
  {
    for (DestinationPlace *dst = inv->destinations; dst; dst = dst->next)
    {
      if (strcmp(dst->resolved_name, name) == 0)
      {
        return dst;
      }
    }
  }
  for (Definition *def = blk->definitions; def; def = def->next)
  {
    for (DestinationPlace *dst = def->destinations; dst; dst = dst->next)
    {
      if (strcmp(dst->resolved_name, name) == 0)
      {
        return dst;
      }
    }
  }
  return NULL;
}

static bool all_inputs_ready(Definition *def)
{
  for (SourcePlace *src = def->sources; src; src = src->next)
  {
    if (!src->signal || src->signal == &NULL_SIGNAL || !src->signal->content)
    {
      return false;
    }
  }
  return true;
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

static void append_source(SourcePlace **head, SourcePlace *list)
{
  for (SourcePlace *s = list; s; s = s->next)
  {
    s->next_flat = *head;
    *head = s;
  }
}

static void append_dest(DestinationPlace **head, DestinationPlace *list)
{
  for (DestinationPlace *d = list; d; d = d->next)
  {
    d->next_flat = *head;
    *head = d;
  }
}

void flatten_signal_places(Block *blk)
{
  if (!blk)
    return;

  blk->sources = NULL;
  blk->destinations = NULL;

  // Walk all invocations
  for (Invocation *inv = blk->invocations; inv; inv = inv->next)
  {
    append_source(&blk->sources, inv->sources);
    append_dest(&blk->destinations, inv->destinations);
  }

  // Walk all definitions
  for (Definition *def = blk->definitions; def; def = def->next)
  {
    append_source(&blk->sources, def->sources);
    append_dest(&blk->destinations, def->destinations);

    // Inline invocations within a definition (e.g., NOR contains OR/NOT)
    for (Invocation *inline_inv = def->invocations; inline_inv; inline_inv = inline_inv->next)
    {
      append_source(&blk->sources, inline_inv->sources);
      append_dest(&blk->destinations, inline_inv->destinations);
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

  int side_effects = 0;

  SourcePlace *inv_src = inv->sources;
  SourcePlace *def_src = inv->definition->sources;

  if (!inv_src)
    LOG_WARN("⚠️ Invocation %s has no sources", inv->name);
  if (!def_src)
    LOG_WARN("⚠️ Definition %s has no sources", inv->definition->name);

  while (inv_src && def_src)
  {
    LOG_INFO("🔍 Matching Invocation Source and Definition Source...");

    if (inv_src->signal)
    {
      if (inv_src->signal != &NULL_SIGNAL)
      {
        if (inv_src->signal->content)
        {
          LOG_INFO("🧩 Invocation Source has content: %s", inv_src->signal->content);
        }
        else
        {
          LOG_WARN("⚠️ Invocation Source signal exists but content is NULL");
        }
      }
      else
      {
        LOG_INFO("ℹ️ Invocation Source is NULL_SIGNAL");
      }
    }
    else
    {
      LOG_WARN("❓ Invocation Source signal pointer is NULL");
    }

    if (def_src->resolved_name)
    {
      LOG_INFO("🧬 Definition Source name: %s", def_src->resolved_name);
    }
    else
    {
      LOG_WARN("⚠️ Definition Source has NULL name");
    }

    if (inv_src->signal != &NULL_SIGNAL && inv_src->signal->content)
    {
      if (def_src->signal == &NULL_SIGNAL)
      {
        Signal *new_sig = (Signal *)calloc(1, sizeof(Signal));
        if (!new_sig)
        {
          LOG_ERROR("❌ Memory allocation failed for new Signal");
          break;
        }
        new_sig->content = strdup(inv_src->signal->content);
        new_sig->next = NULL;

        def_src->signal = new_sig;
        side_effects++;

        if (inv_src->resolved_name)
        {
          LOG_INFO("📥 Injected named signal [%s] → Definition SourcePlace %s",
                   inv_src->resolved_name, def_src->resolved_name ? def_src->resolved_name : "(null)");
        }
        else
        {
          LOG_INFO("💎 Injected literal [%s] → Definition SourcePlace %s",
                   inv_src->signal->content, def_src->resolved_name ? def_src->resolved_name : "(null)");
        }
      }
      else
      {
        LOG_INFO("🚫 Definition SourcePlace %s already has signal — skipping",
                 def_src->resolved_name ? def_src->resolved_name : "(null)");
      }
    }
    else
    {
      LOG_WARN("⚠️ Invocation SourcePlace missing usable content — skipping");
    }

    inv_src = inv_src->next;
    def_src = def_src->next;
  }

  // Now that inputs are injected, evaluate ConditionalInvocation if present
  ConditionalInvocation *ci = inv->definition->conditional_invocation;
  if (ci)
  {
    char pattern[128] = {0};
    size_t offset = 0;

    // Build pattern string from resolved_template_args
    for (size_t i = 0; i < ci->arg_count; ++i)
    {
      const char *arg = ci->resolved_template_args[i];
      SourcePlace *src = inv->definition->sources;
      while (src)
      {
        if (src->resolved_name && strcmp(src->resolved_name, arg) == 0) 
        {
          if (src->signal && src->signal->content)
          {
            size_t len = strlen(src->signal->content);
            if (offset + len < sizeof(pattern) - 1)
            {
              memcpy(pattern + offset, src->signal->content, len);
              offset += len;
            }
          }
          else
          {
            LOG_WARN("⚠️ Missing signal content for input '%s' in ConditionalInvocation", arg);
            return side_effects;
          }
          break;
        }
        src = src->next;
      }
    }

    pattern[offset] = '\0';
    LOG_INFO("🔎 Built input pattern for '%s': %s", inv->name, pattern);

    // Look for matching case
    const char *result = NULL;
    for (ConditionalInvocationCase *c = ci->cases; c; c = c->next)
    {
      LOG_INFO("🔎 Attempting to match input pattern for '%s' with %s", c->pattern, pattern);
      if (strcmp(c->pattern, pattern) == 0)
      {
        LOG_INFO("✅ Matched");
        result = c->result;
        break;
      }
    }

    if (!result)
    {
      LOG_WARN("❌ No matching case for pattern: %s", pattern);
      return side_effects;
    }

    // Inject result into DestinationPlace in Definition
    DestinationPlace *dp = inv->definition->destinations;
    while (dp)
    {
      if (ci->output && dp->resolved_name && strcmp(dp->resolved_name, ci->output) == 0)
      {
        if (dp->signal == &NULL_SIGNAL || !dp->signal)
        {
          Signal *sig = (Signal *)calloc(1, sizeof(Signal));
          sig->content = strdup(result);
          dp->signal = sig;
          LOG_INFO("✅ Evaluated ConditionalInvocation → '%s' => %s", ci->output, result);
          side_effects++;
        }
        break;
      }
      dp = dp->next;
    }

    // Now copy back into Invocation's SourcePlace if wired
    SourcePlace *inv_src = inv->sources;
    while (inv_src)
    {
      if (inv_src->resolved_name && strcmp(inv_src->resolved_name, ci->output) == 0)
      {
        if (inv_src->signal == &NULL_SIGNAL || !inv_src->signal)
        {
          Signal *sig = (Signal *)calloc(1, sizeof(Signal));
          sig->content = strdup(result);
          inv_src->signal = sig;
          LOG_INFO("📤 Propagated result to Invocation SourcePlace: %s", inv_src->resolved_name);
        }
        break;
      }
      inv_src = inv_src->next;
    }
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
int eval_definition(Definition *def, Block *blk)
{
  if (!def || !def->conditional_invocation) return 0;
  if (!all_inputs_ready(def)) return 0;

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

  // Build input pattern
  size_t pattern_len = 0;
  for (SourcePlace *src = def->sources; src; src = src->next)
    if (src->signal && src->signal != &NULL_SIGNAL && src->signal->content)
      pattern_len += strlen(src->signal->content);

  char *pattern = (char *)calloc(pattern_len + 1, sizeof(char));
  if (!pattern)
  {
    LOG_ERROR("❌ Memory allocation failed for pattern construction");
    return 0;
  }

  for (SourcePlace *src = def->sources; src; src = src->next)
  {
    if (src->signal && src->signal != &NULL_SIGNAL && src->signal->content)
    {
      char *clean = strdup(src->signal->content);
      if (clean)
      {
        clean[strcspn(clean, "\r\n")] = '\0';
        strcat(pattern, clean);
        free(clean);
      }
    }
  }

  LOG_INFO("🔎 Evaluating definition %s with input pattern [%s]", def->name, pattern);
  if (!def->conditional_invocation->cases)
  {
    LOG_ERROR("🚨 No cases loaded for definition %s!", def->name);
    free(pattern);
    return 0;
  }

  ConditionalInvocationCase *match = def->conditional_invocation->cases;
  while (match)
  {
    LOG_INFO("🧪 Comparing input pattern: '%s' with case pattern: '%s'", pattern, match->pattern);

    if (strcmp(match->pattern, pattern) == 0)
    {
      LOG_INFO("✅ Pattern matched: %s → %s", match->pattern, match->result);
      LOG_INFO("🔎 Lengths — input: %zu, case: %zu", strlen(pattern), strlen(match->pattern));

      DestinationPlace *dst = find_output_by_resolved_name(def->conditional_invocation->resolved_output, def);
      if (dst && dst->signal == &NULL_SIGNAL)
      {
        Signal *new_sig = (Signal *)calloc(1, sizeof(Signal));
        new_sig->content = strdup(match->result);
        dst->signal = new_sig;

        LOG_INFO("✍️ Output written: %s → [%s]", dst->resolved_name, new_sig->content);

        // 🧠 Propagate result to any matching Invocation SourcePlaces
        for (Invocation *inv = blk->invocations; inv; inv = inv->next)
        {

          for (SourcePlace *inv_src = inv->sources; inv_src; inv_src = inv_src->next)
          {
            if (inv_src->resolved_name &&
                dst->resolved_name &&
                strcmp(inv_src->resolved_name, dst->resolved_name) == 0)
            {
              if (!inv_src->signal || inv_src->signal == &NULL_SIGNAL)
              {
                inv_src->signal = new_sig;
                LOG_INFO("📤 Propagated result to Invocation '%s' SourcePlace: %s = [%s]",
                         inv->name, inv_src->resolved_name, new_sig->content);
              }
            }
          }
        }

        free(pattern);
        return 1;
      }
      else
      {
        LOG_WARN("⚠️ No destination ready for output in %s", def->name);
        free(pattern);
        return 0;
      }
    }

    match = match->next;
  }

  LOG_WARN("⚠️ No pattern matched in %s for input [%s]", def->name, pattern);
  free(pattern);
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
  LOG_INFO("⚙️  Starting eval() pass for [%s] (until stabilization)", blk->psi);
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
      int effects = eval_definition(def, blk);
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
      LOG_INFO("✅ Stabilization reached after %d iterations.\n", iteration);
      break;
    }
  }

  if (iteration >= MAX_ITERATIONS)
  {
    LOG_WARN("⚠️ Eval for %s did not stabilize after %d iterations. Bailout "
             "triggered.\n",
             blk->psi, MAX_ITERATIONS);
  }

  return total_side_effects;
}
