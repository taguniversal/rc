#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "eval.h"
#include "log.h"
#include <vulkan/vulkan.h>
#include <assert.h>
#include "spirv.h"
void emit_spirv_asm_file(const char *sexpr_path, const char *spvasm_path)
{
    FILE *in = fopen(sexpr_path, "r");
    if (!in)
    {
        LOG_ERROR("❌ Failed to open input sexpr file: %s", sexpr_path);
        return;
    }

    FILE *out = fopen(spvasm_path, "w");
    if (!out)
    {
        LOG_ERROR("❌ Failed to open output spvasm file: %s", spvasm_path);
        fclose(in);
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), in))
    {
        // Skip to first '('
        char *start = strchr(line, '(');
        if (!start)
            continue;
        start++;

        // Trim ending ')'
        char *end = strrchr(start, ')');
        if (end)
            *end = '\0';

        // Tokenize
        char *tokens[64];
        int count = 0;
        char *tok = strtok(start, " \t\n\r");
        while (tok && count < 64)
        {
            tokens[count++] = tok;
            tok = strtok(NULL, " \t\n\r");
        }

        if (count == 0)
            continue;

        const char *op = tokens[0];

        // List of opcodes that yield a result ID as their first operand
        bool has_result_id = false;
        int result_index = -1;

        if (strcmp(op, "OpTypeVoid") == 0 || strcmp(op, "OpTypeInt") == 0 ||
            strcmp(op, "OpTypeBool") == 0 || strcmp(op, "OpTypeFunction") == 0 ||
            strcmp(op, "OpConstant") == 0 || strcmp(op, "OpFunction") == 0 ||
            strcmp(op, "OpLabel") == 0 || strcmp(op, "OpLoad") == 0 ||
            strcmp(op, "OpIEqual") == 0 || strcmp(op, "OpLogicalAnd") == 0 ||
            strcmp(op, "OpLogicalOr") == 0 || strcmp(op, "OpSelect") == 0)
        {
            has_result_id = true;
            result_index = 1;
        }
        else if (strcmp(op, "OpVariable") == 0 || strcmp(op, "OpStore") == 0)
        {
            has_result_id = true;
            result_index = 1;
        }

        if (has_result_id && count > result_index && tokens[result_index][0] == '%')
        {
            fprintf(out, "%s = %s", tokens[result_index], tokens[0]);
            for (int i = 1; i < count; ++i)
            {
                if (i == result_index)
                    continue; // Already printed as lhs
                fprintf(out, " %s", tokens[i]);
            }
            fprintf(out, "\n");
        }
        else
        {
            fprintf(out, "%s", tokens[0]);
            for (int i = 1; i < count; ++i)
            {
                fprintf(out, " %s", tokens[i]);
            }
            fprintf(out, "\n");
        }
    }

    fclose(in);
    fclose(out);
    LOG_INFO("✅ Converted S-expr to SPIR-V assembly: %s → %s", sexpr_path, spvasm_path);
}
