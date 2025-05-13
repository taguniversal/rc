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


void emit_spirv_asm_file(const char *sexpr_path, const char *spvasm_path) {
    LOG_INFO("✏️ emit_spirv_asm_file not implemented yet: %s -> %s", sexpr_path, spvasm_path);
    // Stub: copy file for now so pipeline doesn't break
    FILE *in = fopen(sexpr_path, "r");
    if (!in) {
        LOG_ERROR("❌ Failed to open input sexpr file: %s", sexpr_path);
        return;
    }
    FILE *out = fopen(spvasm_path, "w");
    if (!out) {
        LOG_ERROR("❌ Failed to open output spvasm file: %s", spvasm_path);
        fclose(in);
        return;
    }

    char buf[1024];
    while (fgets(buf, sizeof(buf), in)) {
        fputs(buf, out);
    }

    fclose(in);
    fclose(out);
    LOG_INFO("✅ Stub copied %s -> %s", sexpr_path, spvasm_path);
}

