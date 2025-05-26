#include "eval_util.h"
#include <string.h>
#include "eval.h"
#include "log.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void append_destination(DestinationPlaceList *list, DestinationPlace *dp)
{
    size_t new_count = list->count + 1;
    DestinationPlace **new_items = realloc(list->items, new_count * sizeof(DestinationPlace *));
    if (!new_items)
    {
        LOG_ERROR("❌ Failed to realloc destination list");
        return;
    }
    list->items = new_items;
    list->items[list->count] = dp;
    list->count = new_count;
}

void append_source(SourcePlaceList *list, SourcePlace *sp)
{
    size_t new_count = list->count + 1;
    SourcePlace **new_items = realloc(list->items, new_count * sizeof(SourcePlace *));
    if (!new_items)
    {
        LOG_ERROR("❌ Failed to realloc source list");
        return;
    }
    list->items = new_items;
    list->items[list->count] = sp;
    list->count = new_count;
}

bool build_input_pattern(Definition *def, char **arg_names, size_t arg_count, char *out_buf, size_t out_buf_size)
{
    if (!def || !arg_names || !out_buf || out_buf_size == 0)
    {
        LOG_ERROR("❌ build_input_pattern: Invalid arguments — def=%p, arg_names=%p, out_buf=%p, out_buf_size=%zu",
                  def, arg_names, out_buf, out_buf_size);
        return false;
    }

    out_buf[0] = '\0';
    size_t offset = 0;

    for (size_t i = 0; i < arg_count; ++i)
    {
        const char *arg = arg_names[i];
        if (!arg)
        {
            LOG_ERROR("❌ build_input_pattern: arg_names[%zu] is NULL", i);
            return false;
        }

        LOG_INFO("🔍 build_input_pattern: Looking for signal for arg[%zu] = '%s'", i, arg);

        bool matched = false;

        for (size_t j = 0; j < def->sources.count; ++j)
        {
            SourcePlace *src = def->sources.items[j];
            if (!src || !src->resolved_name)
                continue;

            LOG_INFO("   ➤ Checking SourcePlace: resolved_name='%s'", src->resolved_name);

            if (strcmp(src->resolved_name, arg) == 0)
            {
                matched = true;
                if (src->content)
                {
                    size_t len = strlen(src->content);
                    if (offset + len >= out_buf_size - 1)
                    {
                        LOG_ERROR("🚨 build_input_pattern: Buffer overflow while appending '%s'", src->content);
                        return false;
                    }

                    memcpy(out_buf + offset, src->content, len);
                    offset += len;
                    out_buf[offset] = '\0';

                    LOG_INFO("✅ Appended signal '%s' to pattern", src->content);
                }
                else
                {
                    LOG_WARN("⚠️ build_input_pattern: Signal or content missing for arg '%s'", arg);
                    return false;
                }
                break;
            }
        }

        if (!matched)
        {
            LOG_WARN("⚠️ build_input_pattern: No SourcePlace matched for arg '%s'", arg);
            return false;
        }
    }

    LOG_INFO("🔎 build_input_pattern: final result = '%s'", out_buf);
    return true;
}

int propagate_content(SourcePlace *src, DestinationPlace *dst)
{
    LOG_INFO("📡 Attempting propagation: src='%s' → dst='%s'",
             src->resolved_name ? src->resolved_name : "(null)",
             dst->resolved_name ? dst->resolved_name : "(null)");

    if (!src || !src->content)
    {
        LOG_INFO("🚫 Skipping: Source is null or has no content");
        return 0;
    }

    LOG_INFO("🔍 Source content: [%s]", src->content);

    if (dst->content && strcmp(src->content, dst->content) == 0)
    {
        LOG_INFO("🛑 No propagation needed: destination already has [%s]", dst->content);
        return 0;
    }

    if (dst->content)
    {
        LOG_INFO("♻️ Replacing destination content [%s] with [%s]", dst->content, src->content);
        free(dst->content);
    }
    else
    {
        LOG_INFO("✅ Copying fresh content to destination: [%s]", src->content);
    }

    dst->content = strdup(src->content);

    LOG_INFO("🔁 Propagated content from %s → %s: [%s]",
             src->resolved_name ? src->resolved_name : "(null)",
             dst->resolved_name ? dst->resolved_name : "(null)",
             dst->content);

    return 1; // Side effect occurred
}

int propagate_intrablock_signals(Block *blk)
{
    int count = 0;

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
                count += propagate_content(src, dst);
            }
        }
    }

    LOG_INFO("🔁 Total signal propagations: %d", count);
    return count;
}

int propagate_por_signals(Definition *def)
{
    int count = 0;

    for (Invocation *a = def->place_of_resolution_invocations; a; a = a->next)
    {
        for (size_t i = 0; i < a->sources.count; ++i)
        {
            SourcePlace *src = a->sources.items[i];
            if (!src || !src->resolved_name || !src->content)
                continue;

            for (Invocation *b = def->place_of_resolution_invocations; b; b = b->next)
            {
                for (size_t j = 0; j < b->destinations.count; ++j)
                {
                    DestinationPlace *dst = b->destinations.items[j];
                    if (!dst || !dst->resolved_name)
                        continue;

                    if (strcmp(src->resolved_name, dst->resolved_name) == 0)
                    {
                        count += propagate_content(src, dst);
                    }
                }
            }
        }
    }

    return count;
}

int propagate_por_outputs_to_definition(Definition *def)
{
    int side_effects = 0;

    for (Invocation *inv = def->place_of_resolution_invocations; inv; inv = inv->next)
    {
        for (size_t i = 0; i < inv->sources.count; ++i)
        {
            SourcePlace *src = inv->sources.items[i];
            if (!src || !src->resolved_name || !src->content)
                continue;

            for (size_t j = 0; j < def->destinations.count; ++j)
            {
                DestinationPlace *dst = def->destinations.items[j];
                if (!dst || !dst->resolved_name)
                    continue;

                if (strcmp(dst->resolved_name, src->resolved_name) == 0)
                {
                    if (!dst->content || strcmp(dst->content, src->content) != 0)
                    {
                        if (dst->content)
                            free(dst->content);
                        dst->content = strdup(src->content);
                        side_effects++;
                        LOG_INFO("🔁 Copied POR source '%s' → def->destination", src->resolved_name);
                    }
                }
            }
        }
    }

    return side_effects;
}



int pull_outputs_from_definition(Block *blk, Invocation *inv)
{
  if (!inv || !inv->definition)
  {
    LOG_WARN("⚠️ pull_outputs_from_definition - either inv or inv->definition is NULL");
    return 0;
  }

  LOG_INFO("🧩 Pulling outputs for Invocation '%s' from Definition '%s'",
           inv->name, inv->definition->name);

  int side_effects = 0;

  // Match destination[i] → source[i] by position only
  size_t def_count = inv->definition->destinations.count;
  size_t inv_count = inv->sources.count;
  size_t limit = def_count < inv_count ? def_count : inv_count;

  for (size_t i = 0; i < limit; ++i)
  {
    DestinationPlace *def_dst = inv->definition->destinations.items[i];
    SourcePlace *inv_src = inv->sources.items[i];

    if (!def_dst || !inv_src)
      continue;

    LOG_INFO("🔢 [%zu] Def output → Invocation source", i);
    LOG_INFO("     def_dst content: %s", def_dst->content ? def_dst->content : "(null)");
    LOG_INFO("     inv_src before : %s", inv_src->content ? inv_src->content : "(null)");

    if (def_dst->content)
    {
      if (!inv_src->content || strcmp(inv_src->content, def_dst->content) != 0)
      {
        if (inv_src->content)
          free(inv_src->content);

        inv_src->content = strdup(def_dst->content);
        LOG_INFO("📤 [%zu] Copied [%s] → Invocation source", i, def_dst->content);
        side_effects++;
      }
      else
      {
        LOG_INFO("🛑 [%zu] Skipped: content already matches [%s]", i, def_dst->content);
      }
    }
    else
    {
      LOG_WARN("⚠️ [%zu] Definition destination has no content", i);
    }

    // Optional: update outer block-level destination if found
    if (def_dst->resolved_name)
    {
      DestinationPlace *outer_dst = find_destination(blk, def_dst->resolved_name);
      if (outer_dst)
      {
        if (!outer_dst->content || strcmp(outer_dst->content, def_dst->content) != 0)
        {
          if (outer_dst->content)
            free(outer_dst->content);
          outer_dst->content = def_dst->content ? strdup(def_dst->content) : NULL;
          LOG_INFO("🌐 [%zu] Copied [%s] → Block-level destination [%s]",
                   i, def_dst->content, outer_dst->resolved_name);
          side_effects++;
        }
        else
        {
          LOG_INFO("🌐 [%zu] Block-level destination already has matching content [%s]", i, outer_dst->content);
        }
      }
    }
  }

  return side_effects;
}


DestinationPlace *find_destination(Block *blk, const char *name)
{
    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
        for (size_t i = 0; i < inv->destinations.count; ++i)
        {
            DestinationPlace *dst = inv->destinations.items[i];
            if (!dst || !dst->resolved_name)
                continue;

            LOG_INFO("🔍 Matching: '%s' == '%s'", dst->resolved_name, name);

            if (strcmp(dst->resolved_name, name) == 0)
            {
                return dst;
            }
        }
    }

    for (Definition *def = blk->definitions; def; def = def->next)
    {
        for (size_t i = 0; i < def->destinations.count; ++i)
        {
            DestinationPlace *dst = def->destinations.items[i];
            if (!dst || !dst->resolved_name)
                continue;

            LOG_INFO("🔍 Matching: '%s' == '%s'", dst->resolved_name, name);

            if (strcmp(dst->resolved_name, name) == 0)
            {
                return dst;
            }
        }
    }

    return NULL;
}

bool all_inputs_ready(Definition *def)
{
    bool all_ready = true;

    for (size_t i = 0; i < def->sources.count; ++i)
    {
        SourcePlace *sp = def->sources.items[i];
        if (!sp)
            continue;

        const char *name = sp->resolved_name ? sp->resolved_name : "(unnamed)";
        if (!sp->content)
        {
            LOG_WARN("⚠️ all_inputs_ready: input '%s' is NOT ready (null)", name);
            all_ready = false;
        }
        else
        {
            LOG_INFO("✅ all_inputs_ready: input '%s' = [%s]", name, sp->content);
        }
    }

    return all_ready;
}

const char *match_conditional_case(ConditionalInvocation *ci, const char *pattern)
{
    if (!ci || !pattern)
        return NULL;

    for (ConditionalInvocationCase *c = ci->cases; c; c = c->next)
    {
        LOG_INFO("🔍 Matching: '%s' =='%s'", c->pattern, pattern);
        if (strcmp(c->pattern, pattern) == 0)
        {
            LOG_INFO("✅ match_conditional_case: Pattern matched: %s → %s", c->pattern, c->result);
            return c->result;
        }
    }

    LOG_WARN("⚠️ match_conditional_case: No match for pattern: %s", pattern);
    return NULL;
}

int write_result_to_named_output(Definition *def, const char *output_name, const char *result)
{
    LOG_INFO("📤 Attempting to write result [%s] to output '%s' in definition '%s'",
             result, output_name, def->name);

    LOG_INFO("🔍 Available Destinations in %s:", def->name);
    for (size_t i = 0; i < def->destinations.count; ++i)
    {
        DestinationPlace *dst = def->destinations.items[i];
        if (dst && dst->resolved_name)
            LOG_INFO("   ➤ Destination: %s", dst->resolved_name);
    }

    for (size_t i = 0; i < def->destinations.count; ++i)
    {
        DestinationPlace *dst = def->destinations.items[i];
        if (!dst || !dst->resolved_name)
            continue;

        LOG_INFO("🔍 Matching: '%s' == '%s'", dst->resolved_name, output_name);

        if (strcmp(dst->resolved_name, output_name) == 0)
        {
            const char *existing = dst->content;

            if (existing && strcmp(existing, result) == 0)
            {
                LOG_INFO("🛑 Output '%s' already matches result [%s], skipping write", dst->resolved_name, result);
                return 0; // No change
            }

            if (existing)
                free(dst->content);

            dst->content = strdup(result);
            LOG_INFO("✍️ Output written: %s → [%s]", dst->resolved_name, result);
            return 1; // Side effect occurred
        }
    }

    LOG_WARN("⚠️ Could not find destination '%s' in definition '%s'", output_name, def->name);
    return -1; // Not found
}

void print_signal_places(Block *blk)
{
    LOG_INFO("🔍 Flattened Signal Places:");

    for (size_t i = 0; i < blk->sources.count; ++i)
    {
        SourcePlace *src = blk->sources.items[i];
        if (src)
        {
            LOG_INFO("Source: %s [%s]",
                     src->resolved_name ? src->resolved_name : "(null)",
                     src->content ? src->content : "(null)");
        }
    }

    for (size_t i = 0; i < blk->destinations.count; ++i)
    {
        DestinationPlace *dst = blk->destinations.items[i];
        if (dst)
        {
            LOG_INFO("Destination: %s [%s]",
                     dst->resolved_name ? dst->resolved_name : "(null)",
                     dst->content ? dst->content : "(null)");
        }
    }
}
int sync_invocation_outputs_to_definition(Definition *def)
{
    if (!def)
        return 0;

    int side_effects = 0;

    for (Invocation *inv = def->place_of_resolution_invocations; inv; inv = inv->next)
    {
        for (size_t i = 0; i < inv->sources.count; ++i)
        {
            SourcePlace *src = inv->sources.items[i];
            if (!src || !src->resolved_name || !src->content)
                continue;

            for (size_t j = 0; j < def->destinations.count; ++j)
            {
                DestinationPlace *dst = def->destinations.items[j];
                if (!dst || !dst->resolved_name)
                    continue;

                if (strcmp(dst->resolved_name, src->resolved_name) == 0)
                {
                    if (!dst->content || strcmp(dst->content, src->content) != 0)
                    {
                        if (dst->content)
                            free(dst->content);
                        dst->content = strdup(src->content);
                        side_effects++;
                        LOG_INFO("🔁 Synced inner output to definition: %s → %s [%s]",
                                 src->resolved_name, dst->resolved_name, dst->content);
                    }
                }
            }
        }
    }

    return side_effects;
}

Definition *find_definition_by_name(Block *blk, const char *name)
{
    if (!blk || !name)
        return NULL;

    for (Definition *def = blk->definitions; def; def = def->next)
    {
        if (def->name && strcmp(def->name, name) == 0)
        {
            return def;
        }
    }

    return NULL; // Not found
}

void link_por_invocations_to_definitions(Block *blk)
{
    for (Definition *def = blk->definitions; def; def = def->next)
    {
        for (Invocation *inv = def->place_of_resolution_invocations; inv; inv = inv->next)
        {
            if (!inv->definition)
            {
                inv->definition = find_definition_by_name(blk, inv->name);
                if (inv->definition)
                {
                    LOG_INFO("🔗 Linked POR Invocation %s → Definition %s", inv->name, inv->definition->name);
                }
                else
                {
                    LOG_WARN("⚠️ POR Invocation %s has no matching Definition!", inv->name);
                }
            }
        }
    }
}

int propagate_definition_signals(Definition *def, Invocation *inv)
{
    int count = 0;
    if (!def || !inv)
    {
        LOG_WARN("⚠️ propagate_definition_signals called with null definition or invocation");
        return 0;
    }

    LOG_INFO("🔄 propagate_definition_signals: [%s] → [%s]", def->name, inv->name);

    for (size_t i = 0; i < def->destinations.count; ++i)
    {
        DestinationPlace *def_dst = def->destinations.items[i];
        if (!def_dst || !def_dst->resolved_name || !def_dst->content)
            continue;

        for (size_t j = 0; j < inv->sources.count; ++j)
        {
            SourcePlace *inv_src = inv->sources.items[j];
            if (!inv_src || !inv_src->resolved_name)
                continue;

            if (strcmp(inv_src->resolved_name, def_dst->resolved_name) == 0)
            {
                LOG_INFO("🔁 propagate_definition_signals: %s → %s [%s]",
                         inv_src->resolved_name, def_dst->resolved_name, inv_src->content);
                count += propagate_content(inv_src, def_dst);
            }
        }
    }

    return count;
}
