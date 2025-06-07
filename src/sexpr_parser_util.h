#include "eval.h"
#include <stdbool.h>
int parse_signal(SExpr *sig_expr, char **out_content);
bool parse_definition_name(Definition *def, SExpr *expr);
void parse_definition_inputs(Definition *def, const SExpr *expr);
void parse_definition_destinations(Definition *def, SExpr *expr);

void parse_place_of_resolution(Definition *def, SExpr *expr, char **pending_output);
void parse_conditional_invocation_block(Definition *def, SExpr *expr, char **pending_output);
void parse_por_invocations(Definition *def, SExpr *expr);
bool parse_invocation_name(Invocation *inv, SExpr *expr);
void parse_invocation_destinations(Invocation *inv, SExpr *expr);
void parse_invocation_sources(Invocation *inv, SExpr *expr);


