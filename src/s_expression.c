#include "s_expression.h"
#include <stdlib.h>
#include <string.h>

SExprNode *s_expr_new(const char *value) {
    SExprNode *node = malloc(sizeof(SExprNode));
    node->value = strdup(value);
    node->children = NULL;
    node->child_count = 0;
    return node;
}

void s_expr_add_child(SExprNode *parent, SExprNode *child) {
    parent->children = realloc(parent->children, sizeof(SExprNode*) * (parent->child_count + 1));
    parent->children[parent->child_count++] = child;
}

void s_expr_print(FILE *out, SExprNode *node, int indent) {
    fprintf(out, "%*s(%s", indent * 2, "", node->value);
    for (size_t i = 0; i < node->child_count; ++i) {
        fprintf(out, "\n");
        s_expr_print(out, node->children[i], indent + 1);
    }
    fprintf(out, ")");
}

void s_expr_free(SExprNode *node) {
    free(node->value);
    for (size_t i = 0; i < node->child_count; ++i) {
        s_expr_free(node->children[i]);
    }
    free(node->children);
    free(node);
}
