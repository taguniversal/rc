
#ifndef INVOCATION_H
#define INVOCATION_H

#include "string_list.h"
#include <netinet/in.h>

typedef struct in6_addr  psi128_t;

typedef struct {
    char *name;
    char *value;
} LiteralBinding;

typedef struct {
    LiteralBinding *items;
    size_t count;
} LiteralBindingList;


typedef struct Invocation
{
    char *target_name;
    char* origin_sexpr_path;
    int instance_id;
    psi128_t psi;
    psi128_t to;                // target block psi
    StringList *input_signals;  // Ordered signal names used as inputs
    StringList *output_signals; // Ordered signal names used as inputs
    LiteralBindingList *literal_bindings;
    struct Invocation  *next;
} Invocation;


typedef struct
{
    char *pattern;
    char *result;
} ConditionalCase;

typedef struct ConditionalInvocation
{
    char **pattern_args;         // Array of input names like ["A", "B"]
    size_t arg_count;            // Number of pattern arguments
    char *output;                // Output signal name
    ConditionalCase *cases;      // Case list for pattern → result
    size_t case_count;           // Number of conditional cases
} ConditionalInvocation;


typedef enum {
    BODY_SIGNAL_INPUT,
    BODY_SIGNAL_OUTPUT,
    BODY_INVOCATION
} BodyItemType;

typedef struct BodyItem {
    BodyItemType type;
    union {
        char *signal_name;       // For input/output signals
        Invocation *invocation;  // For invocations
    } data;
    struct BodyItem *next;
} BodyItem;

typedef struct Definition
{
    char *name;                  // e.g., "AND", "XOR"
    char *origin_sexpr_path;     // Path to original S-expression source
    char *sexpr_logic;           // The actual logic (e.g., (COND ...))
    StringList *input_signals;   // Ordered signal names used as inputs
    StringList *output_signals; // Ordered set of output signal names
    BodyItem *body;
    ConditionalInvocation *conditional_invocation;
    struct Definition *next;
} Definition;

#endif
