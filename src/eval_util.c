#include <string.h>
#include "eval.h"
#include "log.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

bool build_input_pattern(Definition *def, const char **arg_names, size_t arg_count, char *out_buf, size_t out_buf_size)
{
    if (!def || !arg_names || !out_buf || out_buf_size == 0)
        return false;

    out_buf[0] = '\0'; // start empty
    size_t offset = 0;

    for (size_t i = 0; i < arg_count; ++i)
    {
        const char *arg = arg_names[i];
        SourcePlace *src = def->sources;

        while (src)
        {
            if (src->resolved_name && strcmp(src->resolved_name, arg) == 0)
            {
                if (src->signal && src->signal->content)
                {
                    size_t len = strlen(src->signal->content);
                    if (offset + len >= out_buf_size - 1)
                    {
                        LOG_ERROR("🚨 build_input_pattern: Buffer overflow while appending '%s'", src->signal->content);
                        return false;
                    }

                    memcpy(out_buf + offset, src->signal->content, len);
                    offset += len;
                    out_buf[offset] = '\0';
                }
                else
                {
                    LOG_WARN("⚠️ build_input_pattern: Missing signal for arg '%s'", arg);
                    return false;
                }
                break;
            }
            src = src->next;
        }
    }

    LOG_INFO("🔎 build_input_pattern: result = %s", out_buf);
    return true;
}

int copy_invocation_inputs(Invocation *inv)
{
    if (!inv || !inv->definition)
        return 0;

    int side_effects = 0;

    DestinationPlace *inv_dst = inv->destinations;
    SourcePlace *def_src = inv->definition->sources;

    while (inv_dst && def_src)
    {
        if (inv_dst->signal != &NULL_SIGNAL && inv_dst->signal && inv_dst->signal->content)
        {
            if (def_src->signal == &NULL_SIGNAL)
            {
                Signal *new_sig = (Signal *)calloc(1, sizeof(Signal));
                new_sig->content = strdup(inv_dst->signal->content);
                def_src->signal = new_sig;
                LOG_INFO("📥 Copied input: %s → %s = [%s]",
                         inv_dst->resolved_name, def_src->resolved_name, new_sig->content);
                side_effects++;
            }
        }

        inv_dst = inv_dst->next;
        def_src = def_src->next;
    }

    return side_effects;
}

void propagate_output_to_invocations(Block *blk, DestinationPlace *dst, Signal *sig)
{
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
                    inv_src->signal = sig;
                    LOG_INFO("📤 Propagated result to Invocation '%s' SourcePlace: %s = [%s]",
                             inv->name, inv_src->resolved_name, sig->content);
                }
            }
        }
    }
}

DestinationPlace *find_destination(Block *blk, const char *name)
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

bool all_inputs_ready(Definition *def)
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

#include "eval_util.h"
#include "log.h"
#include <string.h>

const char *match_conditional_case(ConditionalInvocation *ci, const char *pattern)
{
    if (!ci || !pattern)
        return NULL;

    for (ConditionalInvocationCase *c = ci->cases; c; c = c->next)
    {
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
    DestinationPlace *dst = def->destinations;
    int index = 0;

    while (dst)
    {
        if (dst->resolved_name && strcmp(dst->resolved_name, output_name) == 0)
        {
            if (!dst->signal || dst->signal == &NULL_SIGNAL)
            {
                Signal *sig = (Signal *)calloc(1, sizeof(Signal));
                if (!sig)
                {
                    LOG_ERROR("❌ Failed to allocate signal for output '%s'", output_name);
                    return -1;
                }
                sig->content = strdup(result);
                dst->signal = sig;
                LOG_INFO("✍️ Output written: %s → [%s]", dst->resolved_name, result);
                return index; // return the positional index
            }
            else
            {
                const char *existing = dst->signal->content ? dst->signal->content : "(null)";
                LOG_INFO("⚠️ Output '%s' already has a signal, skipping write. Existing content: [%s]",
                         dst->resolved_name, existing);
                return index;
            }
        }
        dst = dst->next;
        index++;
    }

    LOG_WARN("⚠️ Could not find destination '%s' in definition", output_name);
    return -1;
}

