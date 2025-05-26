
#ifndef EVAL_UTIL_H
#define EVAL_UTIL_H

#include "eval.h"
#include <stdbool.h>

bool build_input_pattern(Definition *def, char **arg_names, size_t arg_count, char *out_buf, size_t out_buf_size);
int pull_outputs_from_definition(Block *blk, Invocation *inv);
int propagate_content(SourcePlace *src, DestinationPlace *dst);
int propagate_definition_signals(Definition *def, Invocation *inv);
int propagate_intrablock_signals(Block *blk);
int propagate_por_signals(Definition *def);
int propagate_por_outputs_to_definition(Definition *def) ;
bool all_inputs_ready(Definition *def);
DestinationPlace *find_destination(Block *blk, const char *name);
const char *match_conditional_case(ConditionalInvocation *ci, const char *pattern);
int write_result_to_named_output(Definition *def, const char *output_name, const char *result);
void print_signal_places(Block *blk);
int sync_invocation_outputs_to_definition(Definition *def);
void link_por_invocations_to_definitions(Block *blk);

Definition *find_definition_by_name(Block *blk, const char *name);
void append_destination(DestinationPlaceList *list, DestinationPlace *dp);
void append_source(SourcePlaceList *list, SourcePlace *sp);
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#endif
