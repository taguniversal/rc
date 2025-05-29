#ifndef EMIT_UTIL_H
#define EMIT_UTIL_H
#include <stdio.h>
void emit_indent(FILE *out, int indent);
void emit_atom(FILE *out, const char *atom);
int mkdir_p(const char *path);
#endif 
