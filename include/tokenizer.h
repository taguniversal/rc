#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Token types
typedef enum {
    TOKEN_IDENTIFIER,    // FULLADD, NOT, AND, OR, etc.
    TOKEN_SIGNAL_REF,    // $X, $SUM, etc.
    TOKEN_OPERATOR,      // NOT, AND, OR
    TOKEN_LEFT_PAREN,    // (
    TOKEN_RIGHT_PAREN,   // )
    TOKEN_LEFT_ANGLE,    // <
    TOKEN_RIGHT_ANGLE,   // >
    TOKEN_LEFT_BRACKET,  // [
    TOKEN_RIGHT_BRACKET, // ]
    TOKEN_COMMA,         // ,
    TOKEN_COLON,         // :
    TOKEN_NUMBER,        // 0, 1
    TOKEN_EOF            // End of input
} TokenType;

// Token structure
typedef struct {
    TokenType type;
    char *value;
} Token;

// Tokenizer state
typedef struct {
    char *input;
    int position;
    int length;
} Tokenizer;

// Function prototypes
void init_tokenizer(Tokenizer *tokenizer, char *input);
Token next_token(Tokenizer *tokenizer);
void free_token(Token token);

#endif
