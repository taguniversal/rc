#ifndef PARSER_H
#define PARSER_H

#include "tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Node types for AST
typedef enum {
    NODE_GATE_DEF,
    NODE_SIGNAL_REF,
    NODE_INPUT_LIST,
    NODE_SIGNAL_OUTPUT,
    NODE_PATTERN_MAPPING,
    NODE_RESOLUTION,
    NODE_EXPRESSION
} NodeType;

// AST Node
typedef struct ASTNode {
    NodeType type;
    char *name;
    struct ASTNode **children;
    int child_count;
} ASTNode;

// Parser state
typedef struct {
    Tokenizer *tokenizer;
    Token current;
} Parser;

// Function prototypes
void init_parser(Parser *parser, Tokenizer *tokenizer);
ASTNode *parse_program(Parser *parser);
void free_ast(ASTNode *node);
void print_ast(ASTNode *node, int depth); // 🔥 Updated to match implementation

#endif
