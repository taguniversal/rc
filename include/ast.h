#ifndef AST_H
#define AST_H

typedef enum {
    EXPR_BINARY,
    EXPR_IDENTIFIER,
    EXPR_NUMBER
} ExprType;

typedef struct Expr {
    ExprType type;
    union {
        struct { struct Expr *left; char *op; struct Expr *right; } binary;
        char *identifier;
        char *number;
    };
} Expr;

typedef struct Statement {
    char *lhs;
    Expr *rhs;
} Statement;

Expr *new_identifier(char *name);
Expr *new_number(char *value);
Expr *new_binary_expr(Expr *left, char *op, Expr *right);
Statement *new_statement(char *lhs, Expr *rhs);
void free_expr(Expr *expr);
void free_statement(Statement *stmt);

#endif
