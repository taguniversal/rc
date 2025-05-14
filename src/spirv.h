#ifndef SPIRV_H
#define SPIRV_H
#include <stddef.h>
#include <stdio.h>
#include "eval.h"
typedef struct SPIRVOp
{
    char *opcode;
    char **operands;
    char *result_id;
    size_t operand_count;
    struct SPIRVOp *next;
} SPIRVOp;

typedef struct ExternalSignal
{
    char *external_name; // e.g., "Not_output"
    char *internal_id;   // e.g., "%NOT_1_var_out"
    struct ExternalSignal *next;
} ExternalSignal;

typedef struct SPIRVModule
{
    SPIRVOp *head;
    SPIRVOp *tail;

    Definition *current_definition; // Needed for registering outputs
    Invocation *current_invocation; // Needed for resolving inputs
} SPIRVModule;

SPIRVModule *spirv_module_new(void);
void emit_op(SPIRVModule *mod, const char *opcode, const char **operands, size_t count);
void write_spirv_module(FILE *out, SPIRVOp *head);
void free_spirv_module(SPIRVModule *mod);
void spirv_parse_block(Block *blk, const char *spirv_out_dir);
SPIRVOp *build_sample_spirv(void);
void emit_spirv_header(SPIRVModule *mod);
SPIRVOp *make_op(const char *opcode, const char **operands, size_t count);
void emit_spirv_block(Block *blk, const char *spirv_frag_dir, const char *out_path);
#endif
