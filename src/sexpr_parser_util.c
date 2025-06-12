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


bool parse_definition_body(Definition *def, SExpr *expr) {
    if (!def || !expr || expr->type != S_EXPR_LIST) return false;

    BodyItem *head = NULL;
    BodyItem **tail = &head;

    for (size_t i = 0; i < expr->count; ++i) {
        SExpr *cur = expr->list[i];
        if (!cur || cur->type != S_EXPR_LIST || cur->count < 1)
            continue;

        if (cur->list[0]->type != S_EXPR_ATOM)
            continue;

        const char *tag = cur->list[0]->atom;

        if (strcmp(tag, "Invocation") == 0) {
            Invocation *inv = parse_invocation(cur);  // Full list passed
            if (!inv) return false;

            BodyItem *item = calloc(1, sizeof(BodyItem));
            item->type = BODY_INVOCATION;
            item->data.invocation = inv;
            *tail = item;
            tail = &item->next;
        }

        else if (strcmp(tag, "Inputs") == 0 || strcmp(tag, "Outputs") == 0) {
            BodyItemType type = strcmp(tag, "Inputs") == 0 ? BODY_SIGNAL_INPUT : BODY_SIGNAL_OUTPUT;

            for (size_t j = 1; j < cur->count; ++j) {
                SExpr *s = cur->list[j];
                if (!s || s->type != S_EXPR_ATOM) continue;

                BodyItem *item = calloc(1, sizeof(BodyItem));
                item->type = type;
                item->data.signal_name = strdup(s->atom);
                *tail = item;
                tail = &item->next;
            }
        }

        else {
            LOG_WARN("⚠️ Unknown tag in Definition Body: %s", tag);
        }
    }

    def->body = head;
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
    if (def->output_signals)
        destroy_string_list(def->output_signals);

    def->output_signals = create_string_list();

    for (size_t i = 1; i < outputs_expr->count; ++i)
    {
        const SExpr *output = outputs_expr->list[i];
        if (output->type != S_EXPR_ATOM)
            continue;

        string_list_add(def->output_signals, output->atom);
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


static void sexpr_to_string_recursive(const SExpr *expr, FILE *out)
{
    if (!expr) return;

    switch (expr->type)
    {
        case S_EXPR_ATOM:
            fprintf(out, "%s", expr->atom);
            break;

        case S_EXPR_LIST:
            fprintf(out, "(");
            for (size_t i = 0; i < expr->count; ++i)
            {
                sexpr_to_string_recursive(expr->list[i], out);
                if (i + 1 < expr->count)
                    fprintf(out, " ");
            }
            fprintf(out, ")");
            break;
    }
}

// === 📦 Public API ===
char *sexpr_to_string(const SExpr *expr)
{
    if (!expr) return NULL;

    // Write to a temporary in-memory buffer using open_memstream
    char *buffer = NULL;
    size_t size = 0;
    FILE *mem = open_memstream(&buffer, &size);
    if (!mem) return NULL;

    sexpr_to_string_recursive(expr, mem);
    fclose(mem);  // flush and finalize buffer

    return buffer; // caller is responsible for free()
}


