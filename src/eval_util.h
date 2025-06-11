
#ifndef EVAL_UTIL_H
#define EVAL_UTIL_H
#include "invocation.h"
#include "eval.h"
#include "instance.h"
#include <stdbool.h>

bool build_input_pattern(Definition *def, char **arg_names, size_t arg_count, char *out_buf, size_t out_buf_size);
const char *match_conditional_case(ConditionalInvocation *ci, const char *pattern);
int write_result_to_named_output(Definition *def, const char *output_name, const char *result);
void print_signal_places(Block *blk);

void link_por_invocations_to_definitions(Block *blk);
void prepare_boundary_ports(Block *blk);


void emit_all_units(Block *blk, const char *dir);
void emit_instance(Instance *instance, const char *out_dir);
void unify_invocations(Block *blk);
static char *prepend_instance_name(const char *unit_name, const char *sig);
void globalize_signal_names(Block *blk);

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#endif
