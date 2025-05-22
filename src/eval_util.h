
#ifndef EVAL_UTIL_H
#define EVAL_UTIL_H

#include "eval.h"
#include <stdbool.h>

bool build_input_pattern(Definition *def, char **arg_names, size_t arg_count, char *out_buf, size_t out_buf_size);
int propagate_output_to_invocations(Block *blk, DestinationPlace *dst, const char *content);
int propagate_content(SourcePlace *src, DestinationPlace *dst);
int propagate_intrablock_signals(Block *blk);
bool all_inputs_ready(Definition *def);
DestinationPlace *find_destination(Block *blk, const char *name);
const char *match_conditional_case(ConditionalInvocation *ci, const char *pattern);
int write_result_to_named_output(Definition *def, const char *output_name, const char *result);
void print_signal_places(Block *blk);
#endif
