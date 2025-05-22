#include "eval_util.h"
#include <string.h>
#include "eval.h"
#include "log.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

bool build_input_pattern(Definition *def, char **arg_names, size_t arg_count, char *out_buf, size_t out_buf_size)
{
    if (!def || !arg_names || !out_buf || out_buf_size == 0)
    {
        LOG_ERROR("❌ build_input_pattern: Invalid arguments — def=%p, arg_names=%p, out_buf=%p, out_buf_size=%zu",
                  def, arg_names, out_buf, out_buf_size);
        return false;
    }

    out_buf[0] = '\0'; // start empty
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

        SourcePlace *src = def->sources;
        if (!src)
        {
            LOG_ERROR("❌ build_input_pattern: def->sources is NULL!");
            return false;
        }

        bool matched = false;
        while (src)
        {
            LOG_INFO("   ➤ Checking SourcePlace: resolved_name='%s'", src->resolved_name);
            LOG_INFO("🔍 Matching: src='%s' dst='%s'", src->resolved_name, arg);

            if (src->resolved_name && strcmp(src->resolved_name, arg) == 0)
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

            src = src->next;
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
    LOG_INFO("📡 Attempting propagation: dst='%s' → src='%s'",
             dst->resolved_name ? dst->resolved_name : "(null)",
             src->resolved_name ? src->resolved_name : "(null)");

    if (!dst->content)
    {
        LOG_INFO("🚫 Skipping: Destination has no content");
        return 0;
    }

    if (src->content)
    {
        LOG_INFO("🔍 Existing src content: [%s]", src->content);
        if (strcmp(src->content, dst->content) == 0)
        {
            LOG_INFO("🛑 No propagation needed: content already matches [%s]", src->content);
            return 0;
        }

        LOG_INFO("♻️ Replacing old src content: [%s] with [%s]", src->content, dst->content);
        free(src->content);
    }
    else
    {
        LOG_INFO("✅ Copying fresh content to src: [%s]", dst->content);
    }

    src->content = strdup(dst->content);

    LOG_INFO("🔁 Propagated content from %s → %s: [%s]",
             dst->resolved_name ? dst->resolved_name : "(null)",
             src->resolved_name ? src->resolved_name : "(null)",
             src->content);

    return 1; // Side effect occurred
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

int propagate_output_to_invocations(Block *blk, DestinationPlace *dst, const char *content)
{

    int side_effects = 0;

    if (!dst || !content)
        return side_effects;

    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
        for (SourcePlace *src = inv->sources; src; src = src->next)
        {
            LOG_INFO("🔍 Matching: src='%s' dst='%s'", src->resolved_name, dst->resolved_name);

            if (src->resolved_name && dst->resolved_name &&
                strcmp(src->resolved_name, dst->resolved_name) == 0)
            {
                side_effects = side_effects + propagate_content(src, dst);
            }
        }
    }
    return side_effects;
}

DestinationPlace *find_destination(Block *blk, const char *name)
{
    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
        for (DestinationPlace *dst = inv->destinations; dst; dst = dst->next)
        {
            LOG_INFO("🔍 Matching: '%s' =='%s'", dst->resolved_name, name);

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
            LOG_INFO("🔍 Matching: '%s' =='%s'", dst->resolved_name, name);
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

    for (SourcePlace *sp = def->sources; sp; sp = sp->next)
    {
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

    DestinationPlace *dst = def->destinations;

    while (dst)
    {
        LOG_INFO("🔍 Matching: '%s' == '%s'", dst->resolved_name, output_name);
        if (dst->resolved_name && strcmp(dst->resolved_name, output_name) == 0)
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

        dst = dst->next;
    }

    LOG_WARN("⚠️ Could not find destination '%s' in definition '%s'", output_name, def->name);
    return -1; // Not found
}

void print_signal_places(Block *blk)
{
    LOG_INFO("🔍 Flattened Signal Places:");
    for (SourcePlace *src = blk->sources; src; src = src->next_flat)
    {
        LOG_INFO("Source: %s [%s]", src->resolved_name, src->content);
    }
    for (DestinationPlace *dst = blk->destinations; dst; dst = dst->next_flat)
    {
        LOG_INFO("Destination: %s [%s]", dst->resolved_name, dst->content);
    }
}
