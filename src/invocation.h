
#ifndef INVOCATION_H
#define INOVCATION_H

#include "string_list.h"

typedef struct in6_addr psi128_t;

typedef struct
{
    char *pattern;
    char *result;
} ConditionalCase;

typedef struct ConditionalInvocation
{
    char *pattern;
    char *output;
    ConditionalCase *cases;
    size_t case_count;
} ConditionalInvocation;

typedef struct Definition
{
    char *name;                  // e.g., "AND", "XOR"
    char *origin_sexpr_path;     // Path to original S-expression source
    char *sexpr_logic;           // The actual logic (e.g., (COND ...))
    StringList *input_signals;   // Ordered signal names used as inputs
    StringList **output_signals; // Ordered set of output signal names
    ConditionalInvocation *conditional_invocation;
    Definition *next;
} Definition;

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
    psi128_t psi;
    psi128_t to;                // target block psi
    StringList *input_signals;  // Ordered signal names used as inputs
    StringList *output_signals; // Ordered signal names used as inputs
    LiteralBindingList *literal_bindings;
    Invocation *next;
} Invocation;

typedef struct Instance 
{
   Definition *definition;
   Invocation *invocation;
   Instance *next;
} Instance;
  

#endif
