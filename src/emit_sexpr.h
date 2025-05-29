#ifndef EMIT_SEXPR_H
#define EMIT_SEXPR_H
#include "eval.h"
#include "stdio.h"
void emit_unit(Unit *unit, const char *out_dir);
void emit_all_units(Block *blk, const char *dir);
void emit_invocation(FILE *out, Invocation *inv, int indent);
void emit_definition(FILE *out, Definition *def, int indent);
void emit_source_list(FILE *out, SourcePlaceList list, int indent, const char *role_hint);
void emit_destination_list(FILE *out, DestinationPlaceList list, int indent, const char *role_hint);
void emit_all_definitions(Block *blk, const char *dirpath);
void emit_all_invocations(Block *blk, const char *dirpath);
#endif
