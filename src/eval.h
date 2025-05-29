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


// === Boundary Structs ===

struct SourcePlace
{
  char *name;     // If pulling from a place, this is set (from=...)
  char *resolved_name; //De-conflicted for entire block by rewrite_signals
  int spirv_id;
  char *content; 
};

struct DestinationPlace
{
  char *name;     // Name of the destination
  char *resolved_name;  //De-conflicted for entire block by rewrite_signals
  int spirv_id;
  char *content; 
};

typedef struct {
  size_t count;
  SourcePlace **items;
} SourcePlaceList;

typedef struct {
  size_t count;
  DestinationPlace **items;
} DestinationPlaceList;

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
  char *resolved_output; //De-conflicted for entire block by qualify_local_signals
  ConditionalInvocationCase *cases; // Linked list
};

// === Core Graph Nodes ===

/*
The Invocation - The invocation associates destination places to form an input boundary and associates
source places to form an output boundary. The behavior model is that the boundaries are completeness
boundaries and that the invocation expresses completeness criterion behavior between its input and
output boundaries When the content at the output boundary is complete the content presented to the input
is complete. and the output is the correct resolution of the content presented to the input boundary.
Invocation boundaries are the boundaries of the expression. They are composition boundaries, coordination
boundaries, and partition boundaries.
*/
struct Invocation
{
  char *name; // Unique name of invocation
  int instance_id; // Assigned during signal rewriting for uniqueness
  char *origin_sexpr_path;
  Definition* definition;
  SourcePlaceList boundary_sources;           // Boundary output points
  DestinationPlaceList  boundary_destinations; // Boundary input points

  Invocation *next;       // Linked list
};

/*
The definition expresses the network of associations between the boundaries of the associated invocation.
A definition is a named syntax structure containing a source list, a destination list.
a place of resolution followed by a place of contained definitions. Definitiocnname is the correspondence name
of the definition. The source list is the input for the definition through which a formed name is received,
and the destination list is the output for the definition through which the results are delivered
The place of resolution is best understood as a bounded pure value expression that can contain association expressions.
*/
struct Definition
{
  char *name; // Logic block name ("AND", "XOR", etc.)
  char *origin_sexpr_path;
  SourcePlaceList boundary_sources;           // Boundary input points
  DestinationPlaceList boundary_destinations; // Boundary output points
  SourcePlaceList place_of_resolution_sources;
  Invocation *place_of_resolution_invocations;
  ConditionalInvocation *conditional_invocation; // Optional logic

  Definition *next;
};

typedef struct {
    char *name;           // Fully flattened name, e.g., "AND.INV.0"
    Definition *definition;
    Invocation *invocation;
} Unit;

typedef struct UnitList {
    Unit *unit;
    struct UnitList *next;
} UnitList;


struct Block
{
  char *psi;               // Unique block ID (from mkrand)
  Invocation *invocations; // Linked list of invocations
  Definition *definitions; // Linked list of definitions
  UnitList *units;
  SourcePlaceList sources;
  DestinationPlaceList destinations;
};


// === API ===
int boundary_link_invocations_by_position(Block *blk);
int eval(Block *blk);
void print_signal_places(Block *blk);
int count_invocations(Definition *def);
void dump_signals(Block *blk);
#endif
