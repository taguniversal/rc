#include "eval.h"
#include "sexpr_parser.h"
#include "log.h"
#include "eval_util.h"
#include <stdbool.h>

int parse_signal(SExpr *sig_expr, char **out_content)
{
    if (!sig_expr || !out_content)
        return 0;

    if (sig_expr->count > 0 &&
        sig_expr->list[0]->type == S_EXPR_ATOM &&
        strcmp(sig_expr->list[0]->atom, "Signal") == 0)
    {
        for (size_t i = 1; i < sig_expr->count; ++i)
        {
            SExpr *child = sig_expr->list[i];
            if (child->type != S_EXPR_LIST || child->count < 2)
                continue;

            if (child->list[0]->type == S_EXPR_ATOM &&
                strcmp(child->list[0]->atom, "Content") == 0 &&
                child->list[1]->type == S_EXPR_ATOM)
            {
                *out_content = strdup(child->list[1]->atom);
                LOG_INFO("💡 parse_signal: Parsed signal content [%s]", *out_content);
                return 1;
            }
        }

        LOG_WARN("⚠️ parse_signal: Signal block found, but no valid Content");
        return 0;
    }

    return 0;
}

bool parse_definition_name(Definition *def, SExpr *expr)
{
    if (!def || !expr)
        return false;

    SExpr *name_expr = get_child_by_tag(expr, "Name");
    const char *name = get_atom_value(name_expr, 1);
    if (!name)
    {
        LOG_ERROR("❌ [Definition:<unknown>] Missing or invalid Name");
        free(def);
        return false;
    }

    def->name = strdup(name);
    return true;
}

void parse_definition_sources(Definition *def, SExpr *expr)
{
    SExpr *src_list = get_child_by_tag(expr, "SourceList");
    if (!src_list)
    {
        LOG_ERROR("❌ [Definition:%s] Missing SourceList", def->name);
        return;
    }

    for (size_t i = 1; i < src_list->count; ++i)
    {
        SExpr *place = src_list->list[i];
        if (place->type != S_EXPR_LIST)
            continue;

        SourcePlace *sp = calloc(1, sizeof(SourcePlace));

        for (size_t j = 1; j < place->count; ++j)
        {
            SExpr *field = place->list[j];
            if (field->type != S_EXPR_LIST || field->count == 0)
                continue;

            const char *tag = get_atom_value(field, 0);
            if (!tag)
                continue;

            if (strcmp(tag, "Signal") == 0)
            {
                for (size_t k = 1; k < field->count; ++k)
                {
                    if (field->list[k]->type == S_EXPR_LIST)
                    {
                        LOG_INFO("🧪 About to call parse_signal with:");
                        print_sexpr(field, 8);
                        parse_signal(field, &sp->content);
                    }
                }
            }
            else if (field->count >= 2)
            {
                const char *val = get_atom_value(field, 1);
                if (!val)
                    continue;

                if (strcmp(tag, "Name") == 0)
                    sp->name = strdup(val);
            }
        }

        // Append to array-style list
        size_t new_count = def->boundary_sources.count + 1;
        def->boundary_sources.items = realloc(def->boundary_sources.items, new_count * sizeof(SourcePlace *));
        def->boundary_sources.items[def->boundary_sources.count] = sp;
        def->boundary_sources.count = new_count;

        LOG_INFO("🧩 Parsed SourcePlace in def %s: name=%s content=%s",
                 def->name,
                 sp->name ? sp->name : "null",
                 sp->content ? sp->content : "null");
    }
}

void parse_definition_destinations(Definition *def, SExpr *expr)
{
    SExpr *dst_list = get_child_by_tag(expr, "DestinationList");
    if (!dst_list)
    {
        LOG_ERROR("❌ [Definition:%s] Missing DestinationList", def->name);
        return;
    }

    for (size_t i = 1; i < dst_list->count; ++i)
    {
        SExpr *place = dst_list->list[i];
        if (place->type != S_EXPR_LIST)
            continue;

        DestinationPlace *dp = calloc(1, sizeof(DestinationPlace));

        for (size_t j = 1; j < place->count; ++j)
        {
            SExpr *field = place->list[j];
            if (field->type != S_EXPR_LIST || field->count == 0)
                continue;

            const char *tag = get_atom_value(field, 0);
            if (!tag)
                continue;

            if (strcmp(tag, "Signal") == 0)
            {
                for (size_t k = 1; k < field->count; ++k)
                {
                    if (field->list[k]->type == S_EXPR_LIST)
                    {
                        LOG_INFO("🧪 About to call parse_signal with:");
                        print_sexpr(field, 8);
                        parse_signal(field, &dp->content);
                    }
                }
            }
            else if (field->count >= 2)
            {
                const char *val = get_atom_value(field, 1);
                if (!val)
                    continue;

                if (strcmp(tag, "Name") == 0)
                    dp->name = strdup(val);
            }
        }

        append_destination(&def->boundary_destinations, dp);

        LOG_INFO("🧩 Parsed DestinationPlace in def %s: name=%s content=%s",
                 def->name,
                 dp->name ? dp->name : "null",
                 dp->content ? dp->content : "null");
    }
}

void parse_place_of_resolution(Definition *def, SExpr *expr, char **pending_output)
{
    SExpr *por_expr = get_child_by_tag(expr, "PlaceOfResolution");
    if (!por_expr)
        return;

    for (size_t i = 1; i < por_expr->count; ++i)
    {
        SExpr *place = por_expr->list[i];
        if (!place || place->type != S_EXPR_LIST || place->count < 1)
            continue;

        const char *tag = get_atom_value(place, 0);
        if (!tag)
            continue;

        if (strcmp(tag, "SourcePlace") == 0)
        {
            SourcePlace *sp = calloc(1, sizeof(SourcePlace));
            const char *content_from = NULL;

            for (size_t j = 1; j < place->count; ++j)
            {
                SExpr *field = place->list[j];
                if (!field || field->type != S_EXPR_LIST || field->count < 2)
                    continue;

                const char *key = get_atom_value(field, 0);
                const char *val = get_atom_value(field, 1);
                if (!key || !val)
                    continue;

                if (strcmp(key, "Name") == 0)
                    sp->name = strdup(val);
                else if (strcmp(key, "ContentFrom") == 0 &&
                         strcmp(val, "ConditionalInvocationResult") == 0)
                    content_from = val;
            }

            if (content_from)
            {
                if (sp->name && pending_output)
                {
                    *pending_output = strdup(sp->name);
                    LOG_INFO("📌 Marked output '%s' as ConditionalInvocation result target", *pending_output);
                    append_source(&def->place_of_resolution_sources, sp);
                    LOG_INFO("🔁 PlaceOfResolution: added SourcePlace '%s' from ConditionalInvocationResult", sp->name);
                }
                else
                {
                    LOG_WARN("⚠️ Skipping unnamed SourcePlace from ConditionalInvocationResult");
                    free(sp);
                }
            }
        }
        else if (strcmp(tag, "Invocation") == 0)
        {
            Invocation *inv = parse_invocation(place);
            if (!inv)
                continue;

            inv->next = def->place_of_resolution_invocations;
            def->place_of_resolution_invocations = inv;

            LOG_INFO("🔁 PlaceOfResolution: added Invocation '%s'", inv->name);
        }
    }
}

void parse_conditional_invocation_block(Definition *def, SExpr *expr, char **pending_output)
{
    SExpr *ci_expr = get_child_by_tag(expr, "ConditionalInvocation");
    if (!ci_expr)
        return;

    def->conditional_invocation = parse_conditional_invocation(ci_expr);
    if (!def->conditional_invocation)
        return;

    LOG_INFO("🧩 ConditionalInvocation parsed for %s", def->name);
    for (ConditionalInvocationCase *c = def->conditional_invocation->cases; c; c = c->next)
    {
        LOG_INFO("    Case %s → %s", c->pattern, c->result);
    }

    // Use explicitly passed pending_output if set
    if (*pending_output && !def->conditional_invocation->output)
    {
        def->conditional_invocation->output = strdup(*pending_output);
        LOG_INFO("📌 Inferred ConditionalInvocation output from pending pointer: %s", *pending_output);
        *pending_output = NULL;
        return;
    }

    // ✅ Fallback: Try to infer from PlaceOfResolution unwired outputs
    if (!def->conditional_invocation->output && def->place_of_resolution_sources.count > 0)
    {
        for (size_t i = 0; i < def->place_of_resolution_sources.count; ++i)
        {
            SourcePlace *sp = def->place_of_resolution_sources.items[i];
            if (sp && !sp->content)
            {
                def->conditional_invocation->output = strdup(sp->name);
                LOG_INFO("📌 Fallback inferred ConditionalInvocation output: %s", sp->name);
                break;
            }
        }
    }
}

void parse_por_invocations(Definition *def, SExpr *expr)
{
    for (size_t i = 1; i < expr->count; ++i)
    {
        SExpr *item = expr->list[i];
        if (!item || item->type != S_EXPR_LIST || item->count < 1)
            continue;

        SExpr *head = item->list[0];
        if (!head || head->type != S_EXPR_ATOM)
            continue;

        if (strcmp(head->atom, "Invocation") != 0)
            continue;

        Invocation *inv = parse_invocation(item);
        if (!inv)
        {
            LOG_ERROR("❌ Failed to parse inline Invocation inside %s", def->name);
            continue;
        }

        inv->next = def->place_of_resolution_invocations;
        def->place_of_resolution_invocations = inv;

        LOG_INFO("🔹 Parsed POR Invocation: %s in %s", inv->name, def->name);
    }
}

// INVOCATIION
bool parse_invocation_name(Invocation *inv, SExpr *expr)
{
    SExpr *name_expr = get_child_by_tag(expr, "Name");
    const char *name = get_atom_value(name_expr, 1);
    if (!name)
    {
        LOG_ERROR("❌ [Invocation] Missing or invalid Name");
        return false;
    }

    inv->name = strdup(name);
    return true;
}

void parse_invocation_destinations(Invocation *inv, SExpr *expr)
{
    SExpr *dst_list = get_child_by_tag(expr, "DestinationList");
    if (!dst_list)
    {
        LOG_ERROR("❌ [Invocation:%s] Missing DestinationList", inv->name);
        return;
    }

    for (size_t i = 1; i < dst_list->count; ++i)
    {
        SExpr *place = dst_list->list[i];
        if (place->type != S_EXPR_LIST)
            continue;

        DestinationPlace *dp = calloc(1, sizeof(DestinationPlace));
        bool has_name = false;

        for (size_t j = 1; j < place->count; ++j)
        {
            SExpr *field = place->list[j];
            if (field->type != S_EXPR_LIST || field->count < 2)
                continue;

            const char *tag = get_atom_value(field, 0);
            if (!tag)
                continue;

            if (strcmp(tag, "Name") == 0)
            {
                const char *val = get_atom_value(field, 1);
                if (val)
                {
                    dp->name = strdup(val);
                    has_name = true;
                }
            }
            else if (strcmp(tag, "Signal") == 0)
            {
                for (size_t k = 1; k < field->count; ++k)
                {
                    if (field->list[k]->type == S_EXPR_LIST)
                    {
                        LOG_INFO("🧪 About to call parse_signal with:");
                        print_sexpr(field, 8);
                        parse_signal(field, &dp->content);
                    }
                }
            }
        }

        if (!has_name)
        {
            char synth[64];
            snprintf(synth, sizeof(synth), "%s.0.in%zu", inv->name, i - 1);
            dp->name = strdup(synth);
            LOG_INFO("🧠 Synthesized destination name: %s", dp->name);
        }

        append_destination(&inv->boundary_destinations, dp);
    }
}

void parse_invocation_sources(Invocation *inv, SExpr *expr)
{
    SExpr *src_list = get_child_by_tag(expr, "SourceList");
    if (!src_list)
    {
        LOG_ERROR("❌ [Invocation:%s] Missing SourceList", inv->name);
        return;
    }

    for (size_t i = 1; i < src_list->count; ++i)
    {
        SExpr *place = src_list->list[i];
        if (place->type != S_EXPR_LIST)
            continue;

        SourcePlace *sp = calloc(1, sizeof(SourcePlace));

        for (size_t j = 1; j < place->count; ++j)
        {
            SExpr *field = place->list[j];
            if (field->type != S_EXPR_LIST || field->count < 2)
                continue;

            const char *tag = get_atom_value(field, 0);
            const char *val = get_atom_value(field, 1);
            if (!tag || !val)
                continue;

            if (strcmp(tag, "Name") == 0)
            {
                sp->name = strdup(val);
            }
            else if (strcmp(tag, "Signal") == 0)
            {
                for (size_t k = 1; k < field->count; ++k)
                {
                    if (field->list[k]->type == S_EXPR_LIST)
                    {
                        LOG_INFO("🧪 About to call parse_signal with:");
                        print_sexpr(field, 8);
                        parse_signal(field, &sp->content);
                    }
                }
            }
        }

        append_source(&inv->boundary_sources, sp);

        LOG_INFO("🧩 Parsed SourcePlace: name=%s, content=%s",
                 sp->name ? sp->name : "null",
                 sp->content ? sp->content : "null");
    }
}
