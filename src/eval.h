#ifndef EVAL_H
#define EVAL_H

#include "sqlite3.h"
#include <stdint.h>
#include <ctype.h>
#include <stddef.h> // for size_t

#define SAFETY_GUARD

// === Forward Declarations ===
typedef struct Signal Signal;
typedef struct SourcePlace SourcePlace;
typedef struct DestinationPlace DestinationPlace;
typedef struct ConditionalInvocation ConditionalInvocation;
typedef struct Invocation Invocation;
typedef struct Definition Definition;
typedef struct Block Block;

// === Core Structures ===

struct Signal
{
  char *content;
  struct Signal *next;
#ifdef SAFETY_GUARD
  uint64_t safety_guard;
#endif
};

extern struct Signal NULL_SIGNAL;

// === Boundary Structs ===

struct SourcePlace
{
  char *name;     // If pulling from a place, this is set (from=...)
  char *resolved_name; //De-conflicted for entire block by rewrite_signals
  int spirv_id;
  Signal *signal; // Points to actual signal (or &NULL_SIGNAL)
  SourcePlace *next;
  SourcePlace *next_flat;
};

struct DestinationPlace
{
  char *name;     // Name of the destination
  char *resolved_name;  //De-conflicted for entire block by rewrite_signals
  int spirv_id;
  Signal *signal; // Points to actual signal (or &NULL_SIGNAL)
  DestinationPlace *next;
  DestinationPlace *next_flat;
};

// === Conditional Invocation for dynamic behavior ===

typedef struct ConditionalInvocationCase
{
  char *pattern; // e.g., "00", "01", etc.
  char *result;  // e.g., "0", "1"
  struct ConditionalInvocationCase *next;
} ConditionalInvocationCase;

struct ConditionalInvocation
{
  char *invocation_template; // e.g., "$A$B"
  char **template_args;      // Array of input names (e.g., L, C, R)
  char **resolved_template_args;
  size_t arg_count;
  char* output;
  char *resolved_output; //De-conflicted for entire block by rewrite_signals
  ConditionalInvocationCase *cases; // Linked list
};

// === Core Graph Nodes ===

struct Invocation
{
  char *name; // Unique name of invocation
  int instance_id; // Assigned during signal rewriting for uniqueness
  SourcePlace *sources;           // Boundary input points
  DestinationPlace *destinations; // Boundary output points

  Definition *definition; // Linked back to definition (if any)
  Invocation *next;       // Linked list
};

struct Definition
{
  char *name; // Logic block name ("AND", "XOR", etc.)

  SourcePlace *sources;           // Expected inputs
  DestinationPlace *destinations; // Expected outputs
  Invocation *invocations;
  ConditionalInvocation *conditional_invocation; // Optional logic

  Definition *next;
};

struct Block
{
  char *psi;               // Unique block ID (from mkrand)
  Invocation *invocations; // Linked list of invocations
  Definition *definitions; // Linked list of definitions
  SourcePlace* sources;
  DestinationPlace* destinations;
};

typedef struct
{
  char *name;     // "AND", "OR", "XOR", etc.
  char *filepath; // ./inv/and.xml
} DefinitionBlueprint;

typedef struct
{
  DefinitionBlueprint **blueprints;
  size_t blueprint_count;
} DefinitionLibrary;

// === API ===
void link_invocations_by_position(Block *blk);
int eval(Block *blk);
void flatten_signal_places(Block *blk);
void print_signal_places(Block *blk);
void wire_by_name_correspondence(Block *blk);
void allocate_signals(Block *blk);
int count_invocations(Definition *def);
#endif
