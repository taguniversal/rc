#include "ast.h"
#include <stdlib.h>
#include <string.h>

Expr *new_identifier(char *name) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_IDENTIFIER;
    expr->identifier = strdup(name);
    return expr;
}

Expr *new_number(char *value) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_NUMBER;
    expr->number = strdup(value);
    return expr;
}

Expr *new_binary_expr(Expr *left, char *op, Expr *right) {
    Expr *expr = malloc(sizeof(Expr));
    expr->type = EXPR_BINARY;
    expr->binary.left = left;
    expr->binary.op = strdup(op);
    expr->binary.right = right;
    return expr;
}

Statement *new_statement(char *lhs, Expr *rhs) {
    Statement *stmt = malloc(sizeof(Statement));
    stmt->lhs = strdup(lhs);
    stmt->rhs = rhs;
    return stmt;
}

void free_expr(Expr *expr) {
    if (!expr) return;
    if (expr->type == EXPR_BINARY) {
        free_expr(expr->binary.left);
        free_expr(expr->binary.right);
        free(expr->binary.op);
    } else if (expr->type == EXPR_IDENTIFIER) {
        free(expr->identifier);
    } else if (expr->type == EXPR_NUMBER) {
        free(expr->number);
    }
    free(expr);
}

void free_statement(Statement *stmt) {
    if (!stmt) return;
    free(stmt->lhs);
    free_expr(stmt->rhs);
    free(stmt);
}
