#ifndef SPIRV_H
#define SPIRV_H

typedef struct SPIRVOp {
    char *opcode;
    char **operands;
    size_t operand_count;
    struct SPIRVOp *next;
} SPIRVOp;

typedef struct SPIRVModule {
    SPIRVOp *head;
    SPIRVOp *tail;
} SPIRVModule;

SPIRVModule *spirv_module_new(void);
void emit_op(SPIRVModule *mod, const char *opcode, const char **operands, size_t count);
void write_spirv_module(FILE *out, SPIRVOp *head);
void free_spirv_module(SPIRVModule *mod);
void spirv_parse_block(Block* blk, const char* spirv_out_dir);
SPIRVOp *build_sample_spirv(void);
void emit_spirv_header(SPIRVModule *mod);
SPIRVOp *make_op(const char *opcode, const char **operands, size_t count);
void emit_spirv_block(Block *blk, const char *spirv_frag_dir, const char *out_path);
#endif




