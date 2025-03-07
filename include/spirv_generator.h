#ifndef SPIRV_GENERATOR_H
#define SPIRV_GENERATOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>      // ✅ Include stdio for file I/O
#include <string.h>     // ✅ Include string.h for memset
#include <spirv/unified1/spirv.h>  // ✅ Ensure SPIR-V opcodes are included

#define SPV_WORD_COUNT_SHIFT 16  // ✅ Define the missing constant

#define SPIRV_MAX_WORDS 1024  // ✅ Define a reasonable max size

extern uint32_t spirv_binary[SPIRV_MAX_WORDS];  // ✅ Ensure size is explicit
extern size_t spirv_binary_size;

void generate_spirv_output(const char *output_file);
void generate_spirv(SpvOp op, uint32_t operand1, uint32_t operand2) ;

#endif // SPIRV_GENERATOR_H
