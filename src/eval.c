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
               def_dst->signal->content, inv_dst->name);
      side_effects++;
    }

    DestinationPlace *outer_dst = find_destination(blk, inv_dst->name);
    if (outer_dst && (outer_dst->signal == &NULL_SIGNAL))
    {
      outer_dst->signal = inv_dst->signal;
      LOG_INFO("🔗 Propagated Invocation Destination [%s] outward", inv_dst->name);
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
      if (strcmp(dst->name, name) == 0)
      {
        return dst;
      }
    }
  }
  for (Definition *def = blk->definitions; def; def = def->next)
  {
    for (DestinationPlace *dst = def->destinations; dst; dst = dst->next)
    {
      if (strcmp(dst->name, name) == 0)
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

void link_invocations(Block *blk)
{
  if (!blk)
    return;

  for (Invocation *inv = blk->invocations; inv; inv = inv->next)
  {
    for (Definition *def = blk->definitions; def; def = def->next)
    {
      if (strcmp(inv->name, def->name) == 0)
      {
        inv->definition = def;
        LOG_INFO("🔗 Linked Invocation %s → Definition %s", inv->name, def->name);

        // Wire outputs (Invocation SourcePlaces → Definition Destinations)
        SourcePlace *src = inv->sources;
        DestinationPlace *dst = def->destinations;
        int i = 0;
        while (src && dst)
        {
          src->signal = dst->signal;
          LOG_INFO("🔌 Wired Output [%d]: %s → %s (0x%p)", i, src->name, dst->name, dst->signal);
          src = src->next;
          dst = dst->next;
          i++;
        }

        // Wire inputs (Invocation Destinations → Definition Sources)
        DestinationPlace *inv_dst = inv->destinations;
        SourcePlace *def_src = def->sources;
        i = 0;
        while (inv_dst && def_src)
        {
          def_src->signal = inv_dst->signal;
          LOG_INFO("🔌 Wired Input  [%d]: %s ← %s (0x%p)", i, def_src->name, inv_dst->name, inv_dst->signal);
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

    if (def_src->name)
    {
      LOG_INFO("🧬 Definition Source name: %s", def_src->name);
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

        if (inv_src->name)
        {
          LOG_INFO("📥 Injected named signal [%s] → Definition SourcePlace %s",
                   inv_src->name, def_src->name ? def_src->name : "(null)");
        }
        else
        {
          LOG_INFO("💎 Injected literal [%s] → Definition SourcePlace %s",
                   inv_src->signal->content, def_src->name ? def_src->name : "(null)");
        }
      }
      else
      {
        LOG_INFO("🚫 Definition SourcePlace %s already has signal — skipping",
                 def_src->name ? def_src->name : "(null)");
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

    // Build pattern string from template_args
    for (size_t i = 0; i < ci->arg_count; ++i)
    {
      const char *arg = ci->template_args[i];
      SourcePlace *src = inv->sources;
      while (src)
      {
        if (src->name && strcmp(src->name, arg) == 0)
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
      if (strcmp(c->pattern, pattern) == 0)
      {
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
      if (ci->output && dp->name && strcmp(dp->name, ci->output) == 0)
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
      if (inv_src->name && strcmp(inv_src->name, inv->name) == 0) // match by name, or smarter if needed
      {
        if (inv_src->signal == &NULL_SIGNAL || !inv_src->signal)
        {
          Signal *sig = (Signal *)calloc(1, sizeof(Signal));
          sig->content = strdup(result);
          inv_src->signal = sig;
          LOG_INFO("📤 Propagated result to Invocation SourcePlace: %s", inv_src->name);
        }
        break;
      }
      inv_src = inv_src->next;
    }
  }

  return side_effects;
}

int eval_definition(Definition *def, Block *blk)
{
  (void)blk;
  if (!def || !def->conditional_invocation)
  {
    return 0; // Nothing to do if no logic defined
  }

  if (!all_inputs_ready(def))
  {
    return 0; // Cannot evaluate yet
  }

  LOG_INFO("🔬 Preparing evaluation for definition: %s", def->name);

  for (SourcePlace *src = def->sources; src; src = src->next)
  {
    if (src->name)
    {
      LOG_INFO("🛰️ SourcePlace name: %s", src->name);
    }
    else if (src->signal && src->signal->content)
    {
      LOG_INFO("💎 SourcePlace literal: %s", src->signal->content);
    }
    else
    {
      LOG_WARN("❓ SourcePlace unnamed and empty?");
    }
  }

  // Build the input pattern string
  size_t pattern_len = 0;
  for (SourcePlace *src = def->sources; src; src = src->next)
  {
    if (src->signal && src->signal != &NULL_SIGNAL && src->signal->content)
    {
      pattern_len += strlen(src->signal->content);
    }
  }

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
      // ✨ Sanitize: trim newline or carriage returns
      char *clean = strdup(src->signal->content);
      if (clean)
      {
        clean[strcspn(clean, "\r\n")] = '\0'; // truncate at newline/carriage
        strcat(pattern, clean);
        free(clean);
      }
    }
  }

  LOG_INFO("🔎 Evaluating definition %s with input pattern [%s]", def->name,
           pattern);
  if (!def->conditional_invocation->cases)
  {
    LOG_ERROR("🚨 No cases loaded for definition %s!", def->name);
    free(pattern);
    return 0;
  }

  // Find matching case
  ConditionalInvocationCase *match = def->conditional_invocation->cases;
  while (match)
  {
    LOG_INFO("🧪 Comparing input pattern: '%s' with case pattern: '%s'",
             pattern, match->pattern);

    if (strcmp(match->pattern, pattern) == 0)
    {
      // MATCH FOUND
      LOG_INFO("✅ Pattern matched: %s → %s", match->pattern, match->result);
      LOG_INFO("🔎 Lengths — input: %zu, case: %zu", strlen(pattern),
               strlen(match->pattern));

      // Set result to first destination
      DestinationPlace *dst = def->destinations;
      if (dst && dst->signal == &NULL_SIGNAL)
      {
        Signal *new_sig = (Signal *)calloc(1, sizeof(Signal));

        new_sig->content = strdup(match->result);
        new_sig->next = NULL;

        dst->signal = new_sig;

        LOG_INFO("✍️ Output written: %s → [%s]", dst->name, new_sig->content);

        free(pattern);
        return 1; // 1 side-effect
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
          LOG_ERROR("❌ Failed to allocate signal for destination '%s' in '%s'", dst->name, inv->name);
          exit(1);
        }
        dst->signal->content = NULL; // Or set default
        LOG_INFO("✅ Allocated Signal %p for Destination '%s' in Invocation '%s'",
                 (void *)dst->signal, dst->name, inv->name);
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
