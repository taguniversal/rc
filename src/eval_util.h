
#ifndef EVAL_UTIL_H
#define EVAL_UTIL_H

#include "eval.h"
#include <stdbool.h>

bool build_input_pattern(Definition *def, const char **arg_names, size_t arg_count, char *out_buf, size_t out_buf_size);
int copy_invocation_inputs(Invocation *inv);
void propagate_output_to_invocations(Block *blk, DestinationPlace *dst, Signal *sig);
bool all_inputs_ready(Definition *def);
DestinationPlace *find_destination(Block *blk, const char *name);
const char *match_conditional_case(ConditionalInvocation *ci, const char *pattern);
int write_result_to_named_output(Definition *def, const char *output_name, const char *result, int* side_effects);
#endif
