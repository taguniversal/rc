#ifndef SEXPR_PARSER_H
#define SEXPR_PARSER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "eval.h"
typedef enum { S_EXPR_ATOM, S_EXPR_LIST } SExprType;

typedef struct SExpr {
    SExprType type;
    char *atom;
    struct SExpr **list;
    size_t count;
} SExpr;

SExpr *parse_sexpr(const char *input);
void print_sexpr(const SExpr *expr, int indent);
const char *get_atom_value(SExpr *list, size_t index);
SExpr *get_child_by_tag(SExpr *parent, const char *tag);
void free_sexpr(SExpr *expr);
int parse_block_from_sexpr(Block *blk, const char *inv_dir);
Invocation *parse_invocation(SExpr *expr);
ConditionalInvocation *parse_conditional_invocation(SExpr *ci_expr);
int rewrite_signals(Block *blk);
void emit_all_definitions(Block *blk, const char *dirpath);
void emit_all_invocations(Block *blk, const char *dirpath);
char *load_file(const char *filename);
void validate_invocation_wiring(Block *blk) ;
#endif // SEXPR_PARSER_H
