#include "spirv_generator.h"

uint32_t spirv_binary[SPIRV_MAX_WORDS];
size_t spirv_binary_size = 0;
static uint32_t id_counter = 1;

// Function to generate a new unique ID
uint32_t get_new_id(void) {
    return id_counter++;
}

void generate_spirv(SpvOp op, uint32_t operand1, uint32_t operand2) {
    (void)op;  // Suppress unused parameter warning if `op` is not used

    uint32_t TypeID = get_new_id();
    memset(spirv_binary, 0, sizeof(spirv_binary));
    spirv_binary_size = 0;

    uint32_t ResultID = get_new_id();
    uint32_t Operand1ID = operand1;
    uint32_t Operand2ID = operand2;
    uint32_t FunctionID = get_new_id();

    // SPIR-V Header
    spirv_binary[spirv_binary_size++] = SpvMagicNumber;
    spirv_binary[spirv_binary_size++] = 0x00010000;  // Version
    spirv_binary[spirv_binary_size++] = 0x0008000D;  // Generator
    spirv_binary[spirv_binary_size++] = id_counter;  // Bound ID
    spirv_binary[spirv_binary_size++] = 0;           // Schema

    // OpCapability Shader
    spirv_binary[spirv_binary_size++] = (SpvOpCapability << SPV_WORD_COUNT_SHIFT) | 2;
    spirv_binary[spirv_binary_size++] = SpvCapabilityShader;

    // OpMemoryModel Logical GLSL450
    spirv_binary[spirv_binary_size++] = (SpvOpMemoryModel << SPV_WORD_COUNT_SHIFT) | 3;
    spirv_binary[spirv_binary_size++] = SpvAddressingModelLogical;
    spirv_binary[spirv_binary_size++] = SpvMemoryModelGLSL450;

    // OpEntryPoint
    spirv_binary[spirv_binary_size++] = (SpvOpEntryPoint << SPV_WORD_COUNT_SHIFT) | 4;
    spirv_binary[spirv_binary_size++] = SpvExecutionModelGLCompute;
    spirv_binary[spirv_binary_size++] = FunctionID;
    spirv_binary[spirv_binary_size++] = 0;

    // Declare integer type
    spirv_binary[spirv_binary_size++] = (SpvOpTypeInt << SPV_WORD_COUNT_SHIFT) | 3;
    spirv_binary[spirv_binary_size++] = TypeID;
    spirv_binary[spirv_binary_size++] = 32;
    spirv_binary[spirv_binary_size++] = 0;

    // OpFunction
    spirv_binary[spirv_binary_size++] = (SpvOpFunction << SPV_WORD_COUNT_SHIFT) | 5;
    spirv_binary[spirv_binary_size++] = TypeID;
    spirv_binary[spirv_binary_size++] = FunctionID;
    spirv_binary[spirv_binary_size++] = SpvFunctionControlMaskNone;
    spirv_binary[spirv_binary_size++] = TypeID;

    // OpLabel (Start of function body)
    uint32_t label_id = get_new_id();
    spirv_binary[spirv_binary_size++] = (SpvOpLabel << SPV_WORD_COUNT_SHIFT) | 2;
    spirv_binary[spirv_binary_size++] = label_id;

    // OpBitwiseAnd
    spirv_binary[spirv_binary_size++] = (SpvOpBitwiseAnd << SPV_WORD_COUNT_SHIFT) | 5;
    spirv_binary[spirv_binary_size++] = TypeID;
    spirv_binary[spirv_binary_size++] = ResultID;
    spirv_binary[spirv_binary_size++] = Operand1ID;
    spirv_binary[spirv_binary_size++] = Operand2ID;

    // OpReturn
    spirv_binary[spirv_binary_size++] = (SpvOpReturn << SPV_WORD_COUNT_SHIFT) | 1;

    // OpFunctionEnd
    spirv_binary[spirv_binary_size++] = (SpvOpFunctionEnd << SPV_WORD_COUNT_SHIFT) | 1;

    printf("✅ SPIR-V generated successfully\n");
}

// Function to write the SPIR-V binary to a file
void generate_spirv_output(const char *filename) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        printf("❌ Error: Failed to open file for writing: %s\n", filename);
        return;
    }
    size_t written = fwrite(spirv_binary, sizeof(uint32_t), spirv_binary_size, file);
    fclose(file);

    if (written != spirv_binary_size) {
        printf("❌ Error: Failed to write complete SPIR-V binary to %s\n", filename);
    } else {
        printf("✅ SPIR-V binary saved to %s\n", filename);
    }
}
