// includes remain the same
#include "log.h"
#include "eval.h"
#include "eval_util.h"
#include "rewrite_util.h"
#include "sexpr_parser.h"
#include "spirv.h"
#include "spirv_asm.h"
#include "wiring.h"

#include <libgen.h>
#include <errno.h>
#include <sys/stat.h>  // for mkdir
#include <string.h>    // for strlen

void mkdir_p(const char *path) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *p = tmp;

    for (char *s = tmp + 1; *s; s++) {
        if (*s == '/') {
            *s = '\0';
            mkdir(tmp, 0755);
            *s = '/';
        }
    }
    mkdir(tmp, 0755);
}

void stage_path_buf(char *buf, size_t bufsize, int stage, const char *backend, const char *base) {
    snprintf(buf, bufsize, "%s/stage/%d/%s", base, stage, backend);
    mkdir_p(buf); // Ensure the directory exists immediately after creating the path
}

void compile_block(Block *blk,
                   const char *inv_dir,
                   const char *out_dir)
{
    char sexpr_stage1_dir[256], spirv_stage1_dir[256];
    char sexpr_stage2_dir[256], spirv_stage2_dir[256];
    char sexpr_stage3_dir[256], spirv_stage3_dir[256];
    char spirv_stage4_dir[256], verilog_stage4_dir[256];
    char spirv_asm_stage5_dir[256], verilog_stage5_dir[256];

    stage_path_buf(sexpr_stage1_dir, sizeof(sexpr_stage1_dir), 1, "sexpr", out_dir);
    stage_path_buf(spirv_stage1_dir, sizeof(spirv_stage1_dir), 1, "spirv", out_dir);
    stage_path_buf(sexpr_stage2_dir, sizeof(sexpr_stage2_dir), 2, "sexpr", out_dir);
    stage_path_buf(spirv_stage2_dir, sizeof(spirv_stage2_dir), 2, "spirv", out_dir);
    stage_path_buf(sexpr_stage3_dir, sizeof(sexpr_stage3_dir), 3, "sexpr", out_dir);
    stage_path_buf(spirv_stage3_dir, sizeof(spirv_stage3_dir), 3, "spirv", out_dir);
    stage_path_buf(spirv_stage4_dir, sizeof(spirv_stage4_dir), 4, "spirv", out_dir);
    stage_path_buf(verilog_stage4_dir, sizeof(verilog_stage4_dir), 4, "verilog", out_dir);
    stage_path_buf(spirv_asm_stage5_dir, sizeof(spirv_asm_stage5_dir), 5, "spirv_asm", out_dir);
    stage_path_buf(verilog_stage5_dir, sizeof(verilog_stage5_dir), 5, "verilog", out_dir);

    LOG_INFO("🎯 Compile-only mode enabled");
    LOG_INFO("📂 Emitting definitions to: %s", out_dir);
    LOG_INFO("📂 Output directories:");
    LOG_INFO("   ├─ Stage 1 S-expr              : %s", sexpr_stage1_dir);
    LOG_INFO("   ├─ Stage 1 SPIR-V              : %s", spirv_stage1_dir);
    LOG_INFO("   ├─ Stage 2 S-expr (rewritten)  : %s", sexpr_stage2_dir);
    LOG_INFO("   ├─ Stage 2 SPIR-V (rewritten)  : %s", spirv_stage2_dir);
    LOG_INFO("   ├─ Stage 3 S-expr (flattened)  : %s", sexpr_stage3_dir);
    LOG_INFO("   ├─ Stage 3 SPIR-V (flattened)  : %s", spirv_stage3_dir);
    LOG_INFO("   ├─ Stage 4 Unified SPIR-V      : %s", spirv_stage4_dir);
    LOG_INFO("   ├─ Stage 4 Unified Verilog     : %s", verilog_stage4_dir);
    LOG_INFO("   ├─ Stage 5 SPIR-V ASM          : %s", spirv_asm_stage5_dir);
    LOG_INFO("   └─ Stage 5 Verilog             : %s", verilog_stage5_dir);

    // Compilation pipeline
    parse_block_from_sexpr(blk, inv_dir);
    rewrite_signals(blk);
    flatten_signal_places(blk);
    propagate_intrablock_signals(blk);
    print_signal_places(blk);
    boundary_link_invocations_by_position(blk);
    link_por_invocations_to_definitions(blk);
    validate_invocation_wiring(blk);
    eval(blk);

    emit_all_definitions(blk, sexpr_stage2_dir);
    emit_all_invocations(blk, sexpr_stage2_dir);

    spirv_parse_block(blk, spirv_stage2_dir);

    char main_sexpr_path[256], main_spvasm_path[256];
    snprintf(main_sexpr_path, sizeof(main_sexpr_path), "%s/main.spvasm.sexpr", spirv_stage4_dir);
    snprintf(main_spvasm_path, sizeof(main_spvasm_path), "%s/main.spvasm", spirv_asm_stage5_dir);

    emit_spirv_block(blk, spirv_stage2_dir, main_sexpr_path);
    emit_spirv_asm_file(spirv_stage3_dir, spirv_asm_stage5_dir);

    dump_wiring(blk);
    dump_signals(blk);
}
