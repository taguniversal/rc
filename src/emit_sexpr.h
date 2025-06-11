#ifndef EMIT_SEXPR_H
#define EMIT_SEXPR_H
#include "eval.h"
#include "stdio.h"
void emit_instance(Instance *instance, const char *out_dir);
void emit_all_units(Block *blk, const char *dir);
void emit_invocation(FILE *out, Invocation *inv, int indent);
void emit_definition(FILE *out, Definition *def, int indent);
void emit_output_signals(FILE *out, StringList *outputs, int indent, const char *role_hint);
void emit_input_signals(FILE *out, StringList *inputs, int indent, const char *role_hint);
void emit_all_definitions(Block *blk, const char *dirpath);
void emit_all_invocations(Block *blk, const char *dirpath);
#endif
