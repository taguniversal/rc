#include "block.h"
#include "invocation.h"
#include <stdlib.h>

Definition *create_definition(const char *name, const char *sexpr_path, const char *logic) {
    Definition *def = malloc(sizeof(Definition));
    if (!def) return NULL;

    def->name = strdup(name);
    def->origin_sexpr_path = sexpr_path ? strdup(sexpr_path) : NULL;
    def->sexpr_logic = logic ? strdup(logic) : NULL;

    def->input_signals = create_string_list();
    def->output_signals = create_string_list();
    def->next = NULL;

    return def;
}

Definition *clone_definition(const Definition *src) {
    if (!src) return NULL;

    Definition *def = malloc(sizeof(Definition));
    if (!def) return NULL;

    def->name = src->name ? strdup(src->name) : NULL;
    def->origin_sexpr_path = src->origin_sexpr_path ? strdup(src->origin_sexpr_path) : NULL;
    def->sexpr_logic = src->sexpr_logic ? strdup(src->sexpr_logic) : NULL;

    def->input_signals = string_list_clone(src->input_signals);
    def->output_signals = string_list_clone(src->output_signals);

    def->body = NULL;
    BodyItem **tail = &def->body;
    for (BodyItem *cur = src->body; cur != NULL; cur = cur->next) {
        BodyItem *item = malloc(sizeof(BodyItem));
        item->type = cur->type;
        item->next = NULL;

        if (cur->type == BODY_SIGNAL_INPUT || cur->type == BODY_SIGNAL_OUTPUT) {
            item->data.signal_name = cur->data.signal_name ? strdup(cur->data.signal_name) : NULL;
        } else if (cur->type == BODY_INVOCATION) {
            item->data.invocation = malloc(sizeof(Invocation));
            memcpy(item->data.invocation, cur->data.invocation, sizeof(Invocation));
        }

        *tail = item;
        tail = &item->next;
    }

    if (src->conditional_invocation) {
        ConditionalInvocation *src_ci = src->conditional_invocation;
        ConditionalInvocation *ci = malloc(sizeof(ConditionalInvocation));
        ci->arg_count = src_ci->arg_count;
        ci->pattern_args = string_list_clone(src_ci->pattern_args);
        ci->output = src_ci->output ? strdup(src_ci->output) : NULL;

        ci->case_count = src_ci->case_count;
        if (ci->case_count > 0) {
            ci->cases = calloc(ci->case_count, sizeof(ConditionalCase));
            for (size_t i = 0; i < ci->case_count; ++i) {
                ci->cases[i].pattern = strdup(src_ci->cases[i].pattern);
                ci->cases[i].result = strdup(src_ci->cases[i].result);
            }
        } else {
            ci->cases = NULL;
        }

        def->conditional_invocation = ci;
    } else {
        def->conditional_invocation = NULL;
    }

    def->next = NULL;
    return def;
}

void destroy_definition(Definition *def) {
    if (!def) return;

    free(def->name);
    free(def->origin_sexpr_path);
    free(def->sexpr_logic);

    destroy_string_list(def->input_signals);
    destroy_string_list(def->output_signals);

    BodyItem *cur = def->body;
    while (cur) {
        BodyItem *next = cur->next;

        if (cur->type == BODY_SIGNAL_INPUT || cur->type == BODY_SIGNAL_OUTPUT) {
            free(cur->data.signal_name);
        } else if (cur->type == BODY_INVOCATION) {
            destroy_invocation(cur->data.invocation);
        }

        free(cur);
        cur = next;
    }

    if (def->conditional_invocation) {
        ConditionalInvocation *ci = def->conditional_invocation;

        destroy_string_list(ci->pattern_args);
        free(ci->output);

        for (size_t i = 0; i < ci->case_count; ++i) {
            free(ci->cases[i].pattern);
            free(ci->cases[i].result);
        }

        free(ci->cases);
        free(ci);
    }

    free(def);
}

Invocation *create_invocation(const char *target_name, psi128_t psi, psi128_t to) {
    Invocation *inv = malloc(sizeof(Invocation));
    if (!inv) return NULL;

    inv->target_name = strdup(target_name);
    inv->psi = psi;
    inv->to = to;

    inv->input_signals = create_string_list();
    inv->output_signals = create_string_list();

    return inv;
}

Invocation *clone_invocation(const Invocation *src) {
    if (!src) return NULL;

    Invocation *inv = malloc(sizeof(Invocation));
    if (!inv) return NULL;

    inv->target_name = src->target_name ? strdup(src->target_name) : NULL;
    inv->psi = src->psi;
    inv->to = src->to;

    inv->input_signals = string_list_clone(src->input_signals);
    inv->output_signals = string_list_clone(src->output_signals);

    if (src->literal_bindings && src->literal_bindings->count > 0) {
        inv->literal_bindings = malloc(sizeof(LiteralBindingList));
        inv->literal_bindings->count = src->literal_bindings->count;
        inv->literal_bindings->items = malloc(sizeof(LiteralBinding) * inv->literal_bindings->count);

        for (size_t i = 0; i < inv->literal_bindings->count; ++i) {
            inv->literal_bindings->items[i].name = strdup(src->literal_bindings->items[i].name);
            inv->literal_bindings->items[i].value = strdup(src->literal_bindings->items[i].value);
        }
    } else {
        inv->literal_bindings = NULL;
    }

    inv->next = NULL;
    return inv;
}

void destroy_invocation(Invocation *inv) {
    if (!inv) return;

    free(inv->target_name);

    destroy_string_list(inv->input_signals);
    destroy_string_list(inv->output_signals);

    if (inv->literal_bindings) {
        for (size_t i = 0; i < inv->literal_bindings->count; ++i) {
            free(inv->literal_bindings->items[i].name);
            free(inv->literal_bindings->items[i].value);
        }
        free(inv->literal_bindings->items);
        free(inv->literal_bindings);
    }

    free(inv);
}
