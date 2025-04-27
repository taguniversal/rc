#ifndef EVAL_H
#define EVAL_H

#include "sqlite3.h"


typedef struct Signal {
  char *name;          // e.g., "A", "Out0"
  char *content;       // e.g., "0", "1", etc. (can NULL if unbound)
  struct Signal *next; // for linked list inside Invocation
} Signal;

typedef struct ConditionalInvocationCase {
    char *pattern; // e.g., "00", "01", etc.
    char *result;  // e.g., "0" or "1"
    struct ConditionalInvocationCase *next;
  } ConditionalInvocationCase;
  
  typedef struct ConditionalInvocation {
    char *invocation_template;        // e.g., "$A$B"
    ConditionalInvocationCase *cases; // linked list of cases
  } ConditionalInvocation;
  
  
typedef struct Invocation {
  char *name; // UUID or unique name of the invocation

  Signal *sources;      // linked list of Source signals
  Signal *destinations; // linked list of Destination signals

  struct Definition
      *definition;         // pointer back to its Definition (if contained)
  struct Invocation *next; // for chaining multiple Invocations in the block
} Invocation;

typedef struct Definition {
    char *name; // e.g., "XOR", "AND", etc.
  
    Signal *sources;      // expected inputs
    Signal *destinations; // expected outputs
  
    // ConditionalInvocation mapping
    ConditionalInvocation *conditional_invocation;
  
    struct Definition *next; // for chaining definitions if needed
  } Definition;


typedef struct Block {
  char *psi;
  Invocation *invocations; // linked list of all invocations in this block
  Definition *definitions; // linked list of all definitions
} Block;

// int cycle(sqlite3* db, const char* block, const char* expr_id) ;
int eval(Block* blk);

#endif 