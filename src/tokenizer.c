#include "tokenizer.h"

// Initialize tokenizer
void init_tokenizer(Tokenizer *tokenizer, char *input) {
    tokenizer->input = input;
    tokenizer->position = 0;
    tokenizer->length = strlen(input);
}

// Helper function to advance the tokenizer
char advance(Tokenizer *tokenizer) {
    if (tokenizer->position < tokenizer->length) {
        return tokenizer->input[tokenizer->position++];
    }
    return '\0';
}

// Helper function to peek at the next character
char peek(Tokenizer *tokenizer) {
    if (tokenizer->position < tokenizer->length) {
        return tokenizer->input[tokenizer->position];
    }
    return '\0';
}

// Function to read an identifier (FULLADD, NOT, AND, OR, etc.)
Token read_identifier(Tokenizer *tokenizer, char first_char) {
    int start = tokenizer->position - 1;
    while (isalnum(peek(tokenizer))) {
        advance(tokenizer);
    }
    int length = tokenizer->position - start;
    
    Token token;
    token.type = TOKEN_IDENTIFIER;
    token.value = strndup(&tokenizer->input[start], length);
    return token;
}

Token read_signal_ref(Tokenizer *tokenizer) {
    int start = tokenizer->position - 1; // Include the '$'
    
    if (!isalpha(peek(tokenizer))) {  
        fprintf(stderr, "[ERROR] Invalid signal reference\n");
        exit(1);
    }

    while (isalnum(peek(tokenizer)) || peek(tokenizer) == '_') {
        advance(tokenizer);
    }

    int length = tokenizer->position - start;

    Token token;
    token.type = TOKEN_SIGNAL_REF;
    token.value = strndup(&tokenizer->input[start], length);
    
    return token;
}




// Function to read a number (0, 1)
Token read_number(Tokenizer *tokenizer, char first_char) {
    Token token;
    token.type = TOKEN_NUMBER;
    token.value = strndup(&first_char, 1);
    return token;
}

Token next_token(Tokenizer *tokenizer) {
    while (tokenizer->position < tokenizer->length) {
        char c = advance(tokenizer);

        // ✅ Skip spaces properly before reading a new token
        while (isspace(c)) {
            c = advance(tokenizer);
        }

        if (isalpha(c)) return read_identifier(tokenizer, c);
        if (isdigit(c)) return read_number(tokenizer, c);
        if (c == '$') return read_signal_ref(tokenizer); // ✅ Properly process signal references
        
        Token token;
        token.value = NULL;


        switch (c) {
            case '(': token.type = TOKEN_LEFT_PAREN; break;
            case ')': token.type = TOKEN_RIGHT_PAREN; break;
            case '<': token.type = TOKEN_LEFT_ANGLE; break;
            case '>': token.type = TOKEN_RIGHT_ANGLE; break;
            case '[': token.type = TOKEN_LEFT_BRACKET; break;
            case ']': token.type = TOKEN_RIGHT_BRACKET; break;
            case ',': token.type = TOKEN_COMMA; break;
            case ':': token.type = TOKEN_COLON; break;
            default:
                fprintf(stderr, "[ERROR] Unexpected character: %c\n", c);
                exit(1);
        }

        return token;
    }

    Token eof_token = {TOKEN_EOF, NULL};
    return eof_token;
}


// Free token memory
void free_token(Token token) {
    if (token.value) free(token.value);
}
