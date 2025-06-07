#include "block.h"
#include "invocation.h"
#include <stdlib.h>


Definition *create_definition(const char *name, const char *sexpr_path, const char *logic) {
    Definition *def = malloc(sizeof(Definition));
    if (!def) return NULL;

    def->name = strdup(name);
    def->origin_sexpr_path = sexpr_path ? strdup(sexpr_path) : NULL;
    def->sexpr_logic = logic ? strdup(logic) : NULL;

    def->input_signals = create_string_set();
    def->output_signals = create_string_set();
    def->next = NULL;

    return def;
}

Invocation *create_invocation(const char *target_name, psi128_t psi, psi128_t to) {
    Invocation *inv = malloc(sizeof(Invocation));
    if (!inv) return NULL;

    inv->target_name = strdup(target_name);
    inv->psi = psi;
    inv->to = to;

    inv->inputs = create_string_set();
    inv->outputs = create_string_set();

    return inv;
}

void destroy_definition(Definition *def) {
    if (!def) return;

    free(def->name);
    free(def->origin_sexpr_path);
    free(def->sexpr_logic);

    destroy_string_set(def->input_signals);
    destroy_string_set(def->output_signals);

    free(def);
}


void destroy_invocation(Invocation *inv) {
    if (!inv) return;

    free(inv->target_name);

    destroy_string_set(inv->inputs);
    destroy_string_set(inv->outputs);

    free(inv);
}
