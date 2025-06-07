#include "eval.h"
#include "sexpr_parser.h"
#include "invocation.h"
#include "log.h"
#include "eval_util.h"
#include <stdbool.h>

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

void parse_definition_inputs(Definition *def, const SExpr *expr)
{
    const SExpr *inputs_expr = get_child_by_tag(expr, "Inputs");
    if (!inputs_expr) {
        LOG_ERROR("❌ [Definition:%s] Missing Inputs field", def->name);
        return;
    }

    size_t input_count = inputs_expr->count - 1;  // exclude the "Inputs" atom
    if (input_count == 0) {
        LOG_WARN("⚠️  [Definition:%s] Inputs field is empty", def->name);
        return;
    }

    destroy_string_list(def->input_signals);  // clear any existing
    def->input_signals = create_string_list();

    for (size_t i = 1; i < inputs_expr->count; ++i) {
        const SExpr *input = inputs_expr->list[i];
        if (input->type != S_EXPR_ATOM) continue;

        string_list_add(def->input_signals, input->atom);
        LOG_INFO("🔌 Added input signal to %s: %s", def->name, input->atom);
    }
}

void parse_definition_outputs(Definition *def, const SExpr *expr)
{
    const SExpr *outputs_expr = get_child_by_tag(expr, "Outputs");
    if (!outputs_expr)
    {
        LOG_ERROR("❌ [Definition:%s] Missing Outputs field", def->name);
        return;
    }

    size_t count = outputs_expr->count - 1;
    if (count == 0)
    {
        LOG_WARN("⚠️  [Definition:%s] Outputs field is empty", def->name);
        return;
    }

    // Free previous outputs if already present
    if (def->output_signals) {
        for (size_t i = 0; def->output_signals[i]; ++i)
            destroy_string_list(def->output_signals[i]);
        free(def->output_signals);
    }

    def->output_signals = calloc(count + 1, sizeof(StringList *));  // +1 for NULL terminator

    for (size_t i = 1; i < outputs_expr->count; ++i)
    {
        const SExpr *output = outputs_expr->list[i];
        if (output->type != S_EXPR_ATOM)
            continue;

        StringList *out_list = create_string_list();
        string_list_add(out_list, output->atom);
        def->output_signals[i - 1] = out_list;

        LOG_INFO("📤 Parsed Output for %s: %s", def->name, output->atom);
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

    inv->target_name = strdup(name);
    return true;
}

void parse_invocation_outputs(Invocation *inv, SExpr *expr)
{
    SExpr *outputs = get_child_by_tag(expr, "Outputs");
    if (!outputs)
    {
        LOG_ERROR("❌ [Invocation:%s] Missing Outputs field", inv->target_name);
        return;
    }

    inv->output_signals = create_string_list();

    for (size_t i = 1; i < outputs->count; ++i)
    {
        const char *signal = get_atom_value(outputs->list[i], 0);
        if (signal)
        {
            string_list_add(inv->output_signals, signal);
            LOG_INFO("📤 Parsed output signal: %s", signal);
        }
    }
}


void parse_invocation_inputs(Invocation *inv, SExpr *expr)
{
    inv->input_signals = create_string_list();
    inv->output_signals = create_string_list();
    inv->literal_bindings = calloc(1, sizeof(LiteralBindingList));

    for (size_t i = 1; i < expr->count; ++i)
    {
        SExpr *child = expr->list[i];
        if (!child || child->type != S_EXPR_LIST || child->count == 0)
            continue;

        const char *tag = get_atom_value(child, 0);
        if (!tag)
            continue;

        // Handle (Inputs ...)
        if (strcmp(tag, "Inputs") == 0)
        {
            for (size_t j = 1; j < child->count; ++j)
            {
                const char *signal = get_atom_value(child->list[j], 0);
                if (signal)
                {
                    string_list_add(inv->input_signals, signal);
                    LOG_INFO("🔌 Input signal added: %s", signal);
                }
            }
        }
        // Handle (Outputs ...)
        else if (strcmp(tag, "Outputs") == 0)
        {
            for (size_t j = 1; j < child->count; ++j)
            {
                const char *signal = get_atom_value(child->list[j], 0);
                if (signal)
                {
                    string_list_add(inv->output_signals, signal);
                    LOG_INFO("⚡ Output signal added: %s", signal);
                }
            }
        }
        // Handle (X 1), (Y 0), etc — assumed to be literal bindings
        else if (child->count == 2)
        {
            const char *key = get_atom_value(child, 0);
            const char *val = get_atom_value(child, 1);

            if (key && val)
            {
                size_t count = inv->literal_bindings->count;
                inv->literal_bindings->items = realloc(inv->literal_bindings->items, (count + 1) * sizeof(LiteralBinding));
                inv->literal_bindings->items[count].name = strdup(key);
                inv->literal_bindings->items[count].value = strdup(val);
                inv->literal_bindings->count++;

                LOG_INFO("📌 Literal bound: %s = %s", key, val);
            }
        }
    }
}

