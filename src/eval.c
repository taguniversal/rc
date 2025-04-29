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
    .name = "NULL",
    .content = NULL,
    .next = NULL,
#ifdef SAFETY_GUARD
    .safety_guard = 0xDEADBEEFCAFEBABE,
#endif
};

/**
 * Executes a single evaluation cycle for an invocation.
 *
 * A cycle attempts to bind all input values, then checks readiness across
 * the input boundary. If the invocation is ready (all inputs have values),
 * it proceeds to:
 *   1. Resolve any dynamic ConditionalInvocation templates.
 *   2. Propagate outputs to named destinations.
 *   3. Mirror values from the invoked Definition's outputs.
 *   4. Resolve entries in the resolution table (e.g. for conditional logic).
 *
 * If any step changes state (e.g. value inserted/removed), a side effect
 * is recorded. The cycle is part of the stabilization loop and will repeat
 * until all invocations in the block have reached steady state.
 */
int cycle(sqlite3 *db, const char *block, const char *invocation_uuid) {
  LOG_INFO("⚙️  Starting cycle() pass for block %s invocation uuid: %s", block,
           invocation_uuid);

  int side_effect = 0;

  return side_effect;
}

static DestinationPlace *find_destination(Block *blk, const char *name) {
  for (Invocation *inv = blk->invocations; inv; inv = inv->next) {
    for (DestinationPlace *dst = inv->destinations; dst; dst = dst->next) {
      if (strcmp(dst->name, name) == 0) {
        return dst;
      }
    }
  }
  for (Definition *def = blk->definitions; def; def = def->next) {
    for (DestinationPlace *dst = def->destinations; dst; dst = dst->next) {
      if (strcmp(dst->name, name) == 0) {
        return dst;
      }
    }
  }
  return NULL;
}

static bool all_inputs_ready(Definition *def) {
  for (SourcePlace *src = def->sources; src; src = src->next) {
    if (!src->signal || src->signal == &NULL_SIGNAL || !src->signal->content) {
      return false;
    }
  }
  return true;
}

void link_invocations(Block *blk) {
  if (!blk)
      return;

  for (Invocation *inv = blk->invocations; inv; inv = inv->next) {
      for (Definition *def = blk->definitions; def; def = def->next) {
          if (strcmp(inv->name, def->name) == 0) {
              inv->definition = def;
              LOG_INFO("🔗 Linked Invocation %s → Definition %s", inv->name, def->name);
              break;
          }
      }
      if (!inv->definition) {
          LOG_WARN("⚠️ No matching Definition found for Invocation %s", inv->name);
      }
  }
}

int eval_invocation(Invocation *inv, Block *blk) {
  if (!inv) {
      LOG_WARN("⚠️ Null Invocation passed to eval_invocation, skipping.");
      return 0;
  }

  if (!inv->definition) {
      LOG_WARN("⚠️ Invocation %s has no linked Definition — skipping", inv->name);
      return 0;
  }

  LOG_INFO("🚀 Evaluating Invocation: %s", inv->name);

  int side_effects = 0;

  SourcePlace *inv_src = inv->sources;
  SourcePlace *def_src = inv->definition->sources;

  while (inv_src && def_src) {
      if (inv_src->signal != &NULL_SIGNAL && inv_src->signal->content) {
          // Literal or named content
          if (def_src->signal == &NULL_SIGNAL) {
              Signal *new_sig = (Signal *)calloc(1, sizeof(Signal));
              new_sig->name = def_src->name ? strdup(def_src->name) : NULL;
              new_sig->content = strdup(inv_src->signal->content);
              new_sig->next = NULL;

              def_src->signal = new_sig;
              side_effects++;

              if (inv_src->name) {
                  LOG_INFO("📥 Injected named signal [%s] → Definition SourcePlace %s", inv_src->name, def_src->name);
              } else {
                  LOG_INFO("💎 Injected literal [%s] → Definition SourcePlace %s", inv_src->signal->content, def_src->name);
              }
          } else {
              LOG_INFO("🚫 Definition SourcePlace %s already has signal — skipping", def_src->name);
          }
      } else {
          LOG_WARN("⚠️ Invocation SourcePlace missing content — skipping");
      }

      inv_src = inv_src->next;
      def_src = def_src->next;
  }

  return side_effects;
}



int eval_definition(Definition *def, Block *blk) {
  if (!def || !def->conditional_invocation) {
    return 0; // Nothing to do if no logic defined
  }

  if (!all_inputs_ready(def)) {
    return 0; // Cannot evaluate yet
  }

  LOG_INFO("🔬 Preparing evaluation for definition: %s", def->name);

  for (SourcePlace *src = def->sources; src; src = src->next) {
    if (src->name) {
      LOG_INFO("🛰️ SourcePlace name: %s", src->name);
    } else if (src->signal && src->signal->content) {
      LOG_INFO("💎 SourcePlace literal: %s", src->signal->content);
    } else {
      LOG_WARN("❓ SourcePlace unnamed and empty?");
    }
  }

  // Build the input pattern string
  size_t pattern_len = 0;
  for (SourcePlace *src = def->sources; src; src = src->next) {
    if (src->signal && src->signal != &NULL_SIGNAL && src->signal->content) {
      pattern_len += strlen(src->signal->content);
    }
  }

  char *pattern = (char *)calloc(pattern_len + 1, sizeof(char));
  if (!pattern) {
    LOG_ERROR("❌ Memory allocation failed for pattern construction");
    return 0;
  }

  for (SourcePlace *src = def->sources; src; src = src->next) {
    if (src->signal && src->signal != &NULL_SIGNAL && src->signal->content) {
      strcat(pattern, src->signal->content);
    }
  }

  LOG_INFO("🔎 Evaluating definition %s with input pattern [%s]", def->name,
           pattern);

  // Find matching case
  ConditionalInvocationCase *match = def->conditional_invocation->cases;
  while (match) {
    if (strcmp(match->pattern, pattern) == 0) {
      // MATCH FOUND
      LOG_INFO("✅ Pattern matched: %s → %s", match->pattern, match->result);

      // Set result to first destination
      DestinationPlace *dst = def->destinations;
      if (dst && dst->signal == &NULL_SIGNAL) {
        Signal *new_sig = (Signal *)calloc(1, sizeof(Signal));
        new_sig->name = strdup(dst->name);
        new_sig->content = strdup(match->result);
        new_sig->next = NULL;

        dst->signal = new_sig;

        LOG_INFO("✍️ Output written: %s → [%s]", dst->name, new_sig->content);

        free(pattern);
        return 1; // 1 side-effect
      } else {
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

int eval(Block *blk) {
  LOG_INFO("⚙️  Starting eval() pass for [%s] (until stabilization)", blk->psi);
  int total_side_effects = 0;
  int iteration = 0;

  while (iteration < MAX_ITERATIONS) {
    int side_effect_this_round = 0;
    iteration++;

    // === 1. Evaluate all Invocations ===
    for (Invocation *inv = blk->invocations; inv; inv = inv->next) {
      int effects = eval_invocation(inv, blk);
      side_effect_this_round += effects;
    }

    // === 2. Evaluate all Definitions ===
    for (Definition *def = blk->definitions; def; def = def->next) {
      int effects = eval_definition(def, blk);
      side_effect_this_round += effects;
    }

    total_side_effects += side_effect_this_round;

    if (side_effect_this_round == 0) {
      LOG_INFO("✅ Stabilization reached after %d iterations.\n", iteration);
      break;
    }
  }

  if (iteration >= MAX_ITERATIONS) {
    LOG_WARN("⚠️ Eval for %s did not stabilize after %d iterations. Bailout "
             "triggered.\n",
             blk->psi, MAX_ITERATIONS);
  }

  return total_side_effects;
}
