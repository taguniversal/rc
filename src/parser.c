#include "parser.h"

// 🔥 Internal function for advancing tokens
static void advance(Parser *parser) {
    parser->current = next_token(parser->tokenizer);
}

void init_parser(Parser *parser, Tokenizer *tokenizer) {
    parser->tokenizer = tokenizer;
    advance(parser);  // Move to the first token
}

ASTNode *parse_program(Parser *parser) {
    ASTNode *root = malloc(sizeof(ASTNode));
    root->type = NODE_GATE_DEF;
    root->name = strdup(parser->current.value); // Function name
    root->child_count = 0;
    root->children = NULL;

    advance(parser); // Move past function name

    // 🔥 Create input list node
    ASTNode *inputs = malloc(sizeof(ASTNode));
    inputs->type = NODE_INPUT_LIST;
    inputs->child_count = 0;
    inputs->children = NULL;

    // Parse inputs
    advance(parser); // Skip '('
    while (parser->current.type == TOKEN_SIGNAL_REF) {
        inputs->children = realloc(inputs->children, sizeof(ASTNode *) * (inputs->child_count + 1));
        inputs->children[inputs->child_count] = malloc(sizeof(ASTNode));
        inputs->children[inputs->child_count]->type = NODE_SIGNAL_REF;
        inputs->children[inputs->child_count]->name = strdup(parser->current.value);
        inputs->child_count++;
        advance(parser);

        if (parser->current.type == TOKEN_COMMA) advance(parser);
    }
    advance(parser); // Skip ')'

    // 🔥 Create output list node
    ASTNode *outputs = malloc(sizeof(ASTNode));
    outputs->type = NODE_SIGNAL_OUTPUT;
    outputs->child_count = 0;
    outputs->children = NULL;

    // Parse outputs
    advance(parser); // Skip '('
    while (parser->current.type == TOKEN_SIGNAL_REF) {
        outputs->children = realloc(outputs->children, sizeof(ASTNode *) * (outputs->child_count + 1));
        outputs->children[outputs->child_count] = malloc(sizeof(ASTNode));
        outputs->children[outputs->child_count]->type = NODE_SIGNAL_REF;
        outputs->children[outputs->child_count]->name = strdup(parser->current.value);
        outputs->child_count++;
        advance(parser);
    }
    advance(parser); // Skip ')'

    // Attach inputs and outputs to root
    root->children = malloc(2 * sizeof(ASTNode *));
    root->children[0] = inputs;
    root->children[1] = outputs;
    root->child_count = 2;

    return root;
}



void free_ast(ASTNode *node) {
    if (!node) return;
    for (int i = 0; i < node->child_count; i++) {
        free_ast(node->children[i]);
    }
    free(node->children);
    free(node);
}
void print_ast(ASTNode *node, int depth) {
    if (!node) return;

    // Indent for tree structure
    for (int i = 0; i < depth; i++) {
        printf("  ");
    }

    // Print the node
    if (node->type == NODE_GATE_DEF) {
        printf("AST Node: %s\n", node->name);
    } else if (node->type == NODE_SIGNAL_REF) {
        printf("  ├── %s\n", node->name);
    } else if (node->type == NODE_INPUT_LIST) {
        printf("  ├── Input List:\n");
    } else if (node->type == NODE_SIGNAL_OUTPUT) {
        printf("  ├── Output List:\n");
    } else {
        printf("  ├── Unknown Node\n");
    }

    // Print children
    for (int i = 0; i < node->child_count; i++) {
        print_ast(node->children[i], depth + 1);
    }
}
