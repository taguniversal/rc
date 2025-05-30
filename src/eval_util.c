#include "eval.h"
#include "log.h"
#include "eval_util.h"
#include "block_util.c"
#include <string.h>
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

        for (size_t j = 0; j < def->boundary_sources.count; ++j)
        {
            SourcePlace *src = def->boundary_sources.items[j];
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
    size_t def_count = inv->definition->boundary_destinations.count;
    size_t inv_count = inv->boundary_sources.count;
    size_t limit = def_count < inv_count ? def_count : inv_count;

    for (size_t i = 0; i < limit; ++i)
    {
        DestinationPlace *def_dst = inv->definition->boundary_destinations.items[i];
        SourcePlace *inv_src = inv->boundary_sources.items[i];

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

bool all_inputs_ready(Definition *def)
{
    bool all_ready = true;

    for (size_t i = 0; i < def->boundary_sources.count; ++i)
    {
        SourcePlace *sp = def->boundary_sources.items[i];
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
    for (size_t i = 0; i < def->boundary_destinations.count; ++i)
    {
        DestinationPlace *dst = def->boundary_destinations.items[i];
        if (dst && dst->resolved_name)
            LOG_INFO("   ➤ Destination: %s", dst->resolved_name);
    }

    for (size_t i = 0; i < def->boundary_destinations.count; ++i)
    {
        DestinationPlace *dst = def->boundary_destinations.items[i];
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
        for (size_t i = 0; i < inv->boundary_sources.count; ++i)
        {
            SourcePlace *src = inv->boundary_sources.items[i];
            if (!src || !src->resolved_name || !src->content)
                continue;

            for (size_t j = 0; j < def->boundary_destinations.count; ++j)
            {
                DestinationPlace *dst = def->boundary_destinations.items[j];
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

    for (size_t i = 0; i < def->boundary_destinations.count; ++i)
    {
        DestinationPlace *def_dst = def->boundary_destinations.items[i];
        if (!def_dst || !def_dst->resolved_name || !def_dst->content)
            continue;

        for (size_t j = 0; j < inv->boundary_sources.count; ++j)
        {
            SourcePlace *inv_src = inv->boundary_sources.items[j];
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

Unit *make_unit(const char *name, Definition *def, Invocation *inv)
{
    Unit *unit = malloc(sizeof(Unit));
    if (!unit)
        return NULL;

    unit->name = strdup(name); // strdup is safe for identifiers
    unit->definition = def;
    unit->invocation = inv;

    return unit;
}

void append_unit(Block *blk, Unit *unit)
{
    UnitList *new_node = malloc(sizeof(UnitList));
    if (!new_node)
        return;

    new_node->unit = unit;
    new_node->next = NULL;

    if (!blk->units)
    {
        blk->units = new_node;
    }
    else
    {
        UnitList *current = blk->units;
        while (current->next)
        {
            current = current->next;
        }
        current->next = new_node;
    }
}

void prepare_boundary_ports(Block *blk)
{
    if (!blk)
        return;

    blk->sources.count = 0;
    blk->sources.items = NULL;
    blk->destinations.count = 0;
    blk->destinations.items = NULL;

    LOG_INFO("🔍 Beginning prepare_boundary_ports...");

// Helper macros to add source/dest one-by-one
#define ADD_SRC(sp)                           \
    do                                        \
    {                                         \
        if (sp)                               \
            append_source(&blk->sources, sp); \
    } while (0)
#define ADD_DST(dp)                                     \
    do                                                  \
    {                                                   \
        if (dp)                                         \
            append_destination(&blk->destinations, dp); \
    } while (0)

    // Top-level invocations
    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
        for (size_t i = 0; i < inv->boundary_sources.count; ++i)
        {
            SourcePlace *sp = inv->boundary_sources.items[i];
            LOG_INFO("➕ Source (top-level invocation): %s", sp->resolved_name);
            ADD_SRC(sp);
        }

        for (size_t i = 0; i < inv->boundary_destinations.count; ++i)
        {
            DestinationPlace *dp = inv->boundary_destinations.items[i];
            LOG_INFO("➕ Dest (top-level invocation): %s", dp->resolved_name);
            ADD_DST(dp);
        }
    }

    // Definitions
    for (Definition *def = blk->definitions; def; def = def->next)
    {
        for (size_t i = 0; i < def->boundary_sources.count; ++i)
        {
            SourcePlace *sp = def->boundary_sources.items[i];
            LOG_INFO("➕ Source (definition): %s", sp->resolved_name);
            ADD_SRC(sp);
        }

        for (size_t i = 0; i < def->boundary_destinations.count; ++i)
        {
            DestinationPlace *dp = def->boundary_destinations.items[i];
            LOG_INFO("➕ Dest (definition): %s", dp->resolved_name);
            ADD_DST(dp);
        }

        for (size_t i = 0; i < def->place_of_resolution_sources.count; ++i)
        {
            SourcePlace *por = def->place_of_resolution_sources.items[i];
            LOG_INFO("➕ Source (POR): %s", por->resolved_name);
            ADD_SRC(por);
        }

        for (Invocation *por_inv = def->place_of_resolution_invocations; por_inv; por_inv = por_inv->next)
        {
            for (size_t i = 0; i < por_inv->boundary_sources.count; ++i)
            {
                SourcePlace *sp = por_inv->boundary_sources.items[i];
                LOG_INFO("➕ Source (POR invocation): %s", sp->resolved_name);
                ADD_SRC(sp);
            }

            for (size_t i = 0; i < por_inv->boundary_destinations.count; ++i)
            {
                DestinationPlace *dp = por_inv->boundary_destinations.items[i];
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

static char *prepend_unit_name(const char *unit_name, const char *sig)
{
    if (!sig)
        return NULL;
    size_t len = strlen(unit_name) + 1 + strlen(sig) + 1;
    char *buf = malloc(len);
    snprintf(buf, len, "%s.%s", unit_name, sig);
    return buf;
}

void globalize_signal_names(Block *blk)
{
    LOG_INFO("🌐 Starting signal globalization for all Units...");

    for (UnitList *ul = blk->units; ul != NULL; ul = ul->next)
    {
        Unit *unit = ul->unit;
        const char *unit_name = unit->name;

        // --- Invocation sources and destinations ---
        for (size_t i = 0; i < unit->invocation->boundary_sources.count; ++i)
        {
            SourcePlace *sp = unit->invocation->boundary_sources.items[i];
            if (sp->resolved_name)
            {
                sp->resolved_name = prepend_unit_name(unit_name, sp->resolved_name);
            }
        }
        for (size_t i = 0; i < unit->invocation->boundary_destinations.count; ++i)
        {
            DestinationPlace *dp = unit->invocation->boundary_destinations.items[i];
            if (dp->resolved_name)
            {
                dp->resolved_name = prepend_unit_name(unit_name, dp->resolved_name);
            }
        }

        // --- Definition sources, destinations, POR ---
        for (size_t i = 0; i < unit->definition->boundary_sources.count; ++i)
        {
            SourcePlace *sp = unit->definition->boundary_sources.items[i];
            if (sp->resolved_name)
            {
                sp->resolved_name = prepend_unit_name(unit_name, sp->resolved_name);
            }
        }
        for (size_t i = 0; i < unit->definition->boundary_destinations.count; ++i)
        {
            DestinationPlace *dp = unit->definition->boundary_destinations.items[i];
            if (dp->resolved_name)
            {
                dp->resolved_name = prepend_unit_name(unit_name, dp->resolved_name);
            }
        }

        for (size_t i = 0; i < unit->definition->place_of_resolution_sources.count; ++i)
        {
            SourcePlace *sp = unit->definition->place_of_resolution_sources.items[i];

            const char *base = sp->resolved_name ? sp->resolved_name : sp->name;
            if (base)
            {
                sp->resolved_name = prepend_unit_name(unit_name, base);
            }
        }

        // --- ConditionalInvocation (if present) ---
        ConditionalInvocation *ci = unit->definition->conditional_invocation;
        if (ci)
        {
            for (size_t i = 0; i < ci->arg_count; ++i)
            {
                if (ci->resolved_template_args[i])
                {
                    ci->resolved_template_args[i] =
                        prepend_unit_name(unit_name, ci->resolved_template_args[i]);
                }
            }
            if (ci->resolved_output)
            {
                ci->resolved_output = prepend_unit_name(unit_name, ci->resolved_output);
            }
        }

        LOG_INFO("✅ Signals globalized for unit: %s", unit_name);
    }

    LOG_INFO("🌍 Signal globalization complete.");
}

static Unit *create_unit(const char *def_name, int instance_id, Definition *def, Invocation *inv)
{
    Unit *unit = malloc(sizeof(Unit));
    if (!unit)
        return NULL;

    // Construct unit name: INV.<Definition>.<Instance>
    char buf[256];
    snprintf(buf, sizeof(buf), "INV.%s.%d", def_name, instance_id);
    unit->name = strdup(buf);
    unit->definition = def;
    unit->invocation = inv;

    return unit;
}

Invocation *clone_invocation(const Invocation *src, const char *unit_prefix)
{
    if (!src || !unit_prefix)
        return NULL;

    Invocation *inv = calloc(1, sizeof(Invocation));
    if (!inv)
        return NULL;

    inv->name = strdup(src->name);
    if (!inv->name)
    {
        free(inv);
        return NULL;
    }

    inv->origin_sexpr_path = src->origin_sexpr_path ? strdup(src->origin_sexpr_path) : NULL;

    inv->next = NULL;

    // --- Clone boundary_sources ---
    inv->boundary_sources.count = src->boundary_sources.count;
    inv->boundary_sources.items = calloc(inv->boundary_sources.count, sizeof(SourcePlace *));
    if (!inv->boundary_sources.items && inv->boundary_sources.count > 0)
        goto fail;

    for (size_t i = 0; i < inv->boundary_sources.count; ++i)
    {
        SourcePlace *orig = src->boundary_sources.items[i];
        SourcePlace *copy = calloc(1, sizeof(SourcePlace));
        if (!copy)
            goto fail;

        // 🔁 Explicit deep copy
        copy->name = orig->name ? strdup(orig->name) : NULL;
        copy->resolved_name = orig->resolved_name ? strdup(orig->resolved_name) : NULL;
        copy->content = orig->content ? strdup(orig->content) : NULL;

        // Copy primitive fields here as needed
        // copy->index = orig->index;

        inv->boundary_sources.items[i] = copy;
    }

    // --- Clone boundary_destinations ---
    inv->boundary_destinations.count = src->boundary_destinations.count;
    inv->boundary_destinations.items = calloc(inv->boundary_destinations.count, sizeof(DestinationPlace *));
    if (!inv->boundary_destinations.items && inv->boundary_destinations.count > 0)
        goto fail;

    for (size_t i = 0; i < inv->boundary_destinations.count; ++i)
    {
        DestinationPlace *orig = src->boundary_destinations.items[i];
        DestinationPlace *copy = calloc(1, sizeof(DestinationPlace));
        if (!copy)
            goto fail;

        // 🔁 Explicit deep copy of all fields
        copy->name = orig->name ? strdup(orig->name) : NULL;
        copy->resolved_name = orig->resolved_name ? strdup(orig->resolved_name) : NULL;
        copy->content = orig->content ? strdup(orig->content) : NULL;

        // Copy primitive fields here, if any (e.g. copy->index = orig->index)

        inv->boundary_destinations.items[i] = copy;
    }

    return inv;

fail:
    if (inv)
    {
        free(inv->name);

        for (size_t i = 0; i < inv->boundary_sources.count; ++i)
            free(inv->boundary_sources.items[i]);
        free(inv->boundary_sources.items);

        for (size_t i = 0; i < inv->boundary_destinations.count; ++i)
            free(inv->boundary_destinations.items[i]);
        free(inv->boundary_destinations.items);

        free(inv);
    }

    return NULL;
}

Definition *clone_definition(const Definition *src, const char *unit_prefix)
{
    if (!src || !unit_prefix)
        return NULL;

    Definition *def = calloc(1, sizeof(Definition));
    if (!def)
        return NULL;

    def->name = strdup(src->name);
    if (!def->name)
    {
        free(def);
        return NULL;
    }

    def->origin_sexpr_path = src->origin_sexpr_path ? strdup(src->origin_sexpr_path) : NULL;

    def->next = NULL;

    // --- Clone boundary_sources ---
    def->boundary_sources.count = src->boundary_sources.count;
    def->boundary_sources.items = calloc(def->boundary_sources.count, sizeof(SourcePlace *));
    if (!def->boundary_sources.items && def->boundary_sources.count > 0)
        goto fail;

    for (size_t i = 0; i < def->boundary_sources.count; ++i)
    {
        SourcePlace *orig = src->boundary_sources.items[i];
        SourcePlace *copy = calloc(1, sizeof(SourcePlace));
        copy->name = orig->name ? strdup(orig->name) : NULL;
        copy->resolved_name = orig->resolved_name ? strdup(orig->resolved_name) : NULL;
        copy->content = orig->content ? strdup(orig->content) : NULL;
        // Copy other primitive fields here if needed (e.g. int index, enum flags, etc.)

        def->boundary_sources.items[i] = copy;
    }

    // --- Clone boundary_destinations ---
    def->boundary_destinations.count = src->boundary_destinations.count;
    def->boundary_destinations.items = calloc(def->boundary_destinations.count, sizeof(DestinationPlace *));

    if (!def->boundary_destinations.items && def->boundary_destinations.count > 0)
        goto fail;

    for (size_t i = 0; i < def->boundary_destinations.count; ++i)
    {
        DestinationPlace *orig = src->boundary_destinations.items[i];
        DestinationPlace *copy = calloc(1, sizeof(DestinationPlace));
        if (!copy)
            goto fail;

        // 🔁 Deep copy all fields explicitly
        copy->name = orig->name ? strdup(orig->name) : NULL;
        copy->resolved_name = orig->resolved_name ? strdup(orig->resolved_name) : NULL;
        copy->content = orig->content ? strdup(orig->content) : NULL;

        // If you have any other primitive fields (e.g. flags, enum type, etc.), copy them directly here:
        // copy->some_flag = orig->some_flag;

        def->boundary_destinations.items[i] = copy;
    }

    // --- Clone POR sources ---
    def->place_of_resolution_sources.count = src->place_of_resolution_sources.count;
    def->place_of_resolution_sources.items = calloc(def->place_of_resolution_sources.count, sizeof(SourcePlace *));
    if (!def->place_of_resolution_sources.items && def->place_of_resolution_sources.count > 0)
        goto fail;

    for (size_t i = 0; i < def->place_of_resolution_sources.count; ++i)
    {
        SourcePlace *orig = src->place_of_resolution_sources.items[i];
        SourcePlace *copy = calloc(1, sizeof(SourcePlace));
        if (!copy)
            goto fail;

        // 🔁 Deep copy all relevant fields explicitly
        copy->name = orig->name ? strdup(orig->name) : NULL;
        copy->resolved_name = orig->resolved_name ? strdup(orig->resolved_name) : NULL;
        copy->content = orig->content ? strdup(orig->content) : NULL;

        // Copy any other non-pointer fields here if present:
        // copy->role = orig->role;  // example

        def->place_of_resolution_sources.items[i] = copy;
    }

    // --- Clone POR invocations ---
    def->place_of_resolution_invocations = NULL;
    Invocation **tail = &def->place_of_resolution_invocations;

    for (Invocation *cur = src->place_of_resolution_invocations; cur; cur = cur->next)
    {
        Invocation *copy = clone_invocation(cur, unit_prefix);
        if (!copy)
            goto fail;

        *tail = copy;
        tail = &copy->next;
    }

    return def;

fail:
    // Clean up partial allocation (not full free_tree, just enough to not leak)
    if (def)
    {
        free(def->name);

        for (size_t i = 0; i < def->boundary_sources.count; ++i)
            free(def->boundary_sources.items[i]);
        free(def->boundary_sources.items);

        for (size_t i = 0; i < def->boundary_destinations.count; ++i)
            free(def->boundary_destinations.items[i]);
        free(def->boundary_destinations.items);

        for (size_t i = 0; i < def->place_of_resolution_sources.count; ++i)
            free(def->place_of_resolution_sources.items[i]);
        free(def->place_of_resolution_sources.items);

        Invocation *inv = def->place_of_resolution_invocations;
        while (inv)
        {
            Invocation *next = inv->next;
            free(inv); // optionally call a deeper free here
            inv = next;
        }

        free(def);
    }

    return NULL;
}

void unify_invocations(Block *blk)
{
    LOG_INFO("🏗  Building Unit structures (invocation/definition pairs)...");

    UnitList *head = NULL;
    UnitList **tail = &head;

    size_t count = 0;

    for (Invocation *inv = blk->invocations; inv != NULL; inv = inv->next)
    {
        LOG_INFO("📦 Visiting Invocation: %s (%p)", inv->name, (void *)inv);
    }

    for (Invocation *inv = blk->invocations; inv != NULL; inv = inv->next)
    {
        // Find matching definition
        Definition *def = blk->definitions;
        while (def && strcmp(def->name, inv->name) != 0)
        {
            def = def->next;
        }

        if (!def)
        {
            LOG_WARN("⚠️  No matching definition for invocation: %s", inv->name);
            continue;
        }

        // ✅ Declare and initialize buf BEFORE it's used
        char buf[256];
        snprintf(buf, sizeof(buf), "INV.%s.%d", inv->name, inv->instance_id);

        LOG_INFO("🔗 Creating Unit: %s", buf);

        Definition *def_copy = clone_definition(def, buf);
        Invocation *inv_copy = clone_invocation(inv, buf);

        // Construct Unit
        Unit *unit = malloc(sizeof(Unit));
        unit->name = strdup(buf);
        unit->definition = def_copy;
        unit->invocation = inv_copy;

        // Append to UnitList
        UnitList *node = malloc(sizeof(UnitList));
        node->unit = unit;
        node->next = NULL;

        *tail = node;
        tail = &node->next;
        count += 1;
    }

    blk->units = head;
    LOG_INFO("✅ Finished building %zu unit(s)", count);
}
