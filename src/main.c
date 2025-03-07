#include "parser.h"
#include "tokenizer.h"
#include <stdio.h>

void test_tokenizer(const char *source) {
    Tokenizer tokenizer;
    init_tokenizer(&tokenizer, (char *)source);

    printf("Tokenizing: \"%s\"\n\n", source);

    Token token;
    do {
        token = next_token(&tokenizer);
        printf("Token: Type=%d", token.type);
        if (token.value) {
            printf(", Value=%s", token.value);
        }
        printf("\n");
        free_token(token);
    } while (token.type != TOKEN_EOF);
}

void test_parser(const char *source) {
    Tokenizer tokenizer;
    init_tokenizer(&tokenizer, (char *)source);

    Parser parser;
    init_parser(&parser, &tokenizer);

    printf("\nParsing input: \"%s\"\n\n", source);

    ASTNode *ast = parse_program(&parser);
    if (ast) {
        printf("AST Parsed Successfully:\n");
        print_ast(ast, 0);  // ✅ Pass depth=0 to start
        free_ast(ast);
    } else {
        printf("Parsing failed.\n");
    }
}

int main(void) {
    const char *test_input = "FULLADD($X, $Y, $C)($SUM $CARRY)";

    // Test the tokenizer
    test_tokenizer(test_input);

    // Test the parser (once implemented)
    test_parser(test_input);

    return 0;
}
