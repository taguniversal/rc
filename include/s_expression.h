#ifndef S_EXPRESSION_H
#define S_EXPRESSION_H

#include <stdio.h>

typedef struct SExprNode {
    char *value;
    struct SExprNode **children;
    size_t child_count;
} SExprNode;

SExprNode *s_expr_new(const char *value);
void s_expr_add_child(SExprNode *parent, SExprNode *child);
void s_expr_print(FILE *out, SExprNode *node, int indent);
void s_expr_free(SExprNode *node);

#endif
