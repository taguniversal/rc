// includes remain the same
#include "log.h"
#include "eval.h"
#include "eval_util.h"
#include "rewrite_util.h"
#include "sexpr_parser.h"
#include "emit_util.h"
#include "emit_spirv.h"
#include "emit_sexpr.h"
#include "spirv_asm.h"
#include "wiring.h"
#include "emit_sexpr.h"

#include <libgen.h>
#include <errno.h>
#include <sys/stat.h> // for mkdir
#include <string.h>   // for strlen

void stage_path_buf(char *buf, size_t bufsize, int stage, const char *backend, const char *base)
{
    snprintf(buf, bufsize, "%s/stage/%d/%s", base, stage, backend);
    mkdir_p(buf); // Ensure the directory exists immediately after creating the path
}

void compile_block(Block *blk,
                   const char *inv_dir,
                   const char *out_dir)
{
    char sexpr_stage1_dir[256], spirv_stage1_dir[256];
    char sexpr_stage2_dir[256], spirv_stage2_dir[256];
    char sexpr_stage3_dir[256], spirv_stage3_dir[256], verilog_stage3_dir[256];
    char sexpr_stage4_dir[256];
    char spirv_stage4_dir[256], verilog_stage4_dir[256];
    char spirv_asm_stage5_dir[256], verilog_stage5_dir[256];

    stage_path_buf(sexpr_stage1_dir, sizeof(sexpr_stage1_dir), 1, "sexpr", out_dir);
    stage_path_buf(spirv_stage1_dir, sizeof(spirv_stage1_dir), 1, "spirv", out_dir);
    stage_path_buf(sexpr_stage2_dir, sizeof(sexpr_stage2_dir), 2, "sexpr", out_dir);
    stage_path_buf(spirv_stage2_dir, sizeof(spirv_stage2_dir), 2, "spirv", out_dir);
    stage_path_buf(sexpr_stage3_dir, sizeof(sexpr_stage3_dir), 3, "sexpr", out_dir);
    stage_path_buf(spirv_stage3_dir, sizeof(spirv_stage3_dir), 3, "spirv", out_dir);
    stage_path_buf(verilog_stage3_dir, sizeof(verilog_stage3_dir), 3, "verilog", out_dir);
    stage_path_buf(sexpr_stage4_dir, sizeof(sexpr_stage4_dir), 4, "sexpr", out_dir);
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
    LOG_INFO("   ├─ Stage 3 S-expr (unified)    : %s", sexpr_stage3_dir);
    LOG_INFO("   ├─ Stage 3 SPIR-V (unified)    : %s", spirv_stage3_dir);
    LOG_INFO("   ├─ Stage 4 Flattened S-expr    : %s", sexpr_stage4_dir);
    LOG_INFO("   ├─ Stage 4 Flattened SPIR-V    : %s", spirv_stage4_dir);
    LOG_INFO("   ├─ Stage 4 Flattened Verilog   : %s", verilog_stage4_dir);
    LOG_INFO("   ├─ Stage 5 Final SPIR-V ASM    : %s", spirv_asm_stage5_dir);
    LOG_INFO("   └─ Stage 5 Final Verilog       : %s", verilog_stage5_dir);
    // === Compilation Pipeline ===

    parse_block_from_sexpr(blk, inv_dir);        // Stage 1: Parse raw S-expr files (definitions + invocations)
    emit_all_definitions(blk, sexpr_stage1_dir); // Emit initial logic blocks
    emit_all_invocations(blk, sexpr_stage1_dir); // Emit initial invocations

    qualify_local_signals(blk);                  // Stage 2: Qualify signals within each definition (e.g., AND.A)
    spirv_parse_block(blk, spirv_stage2_dir);    // Emit SPIR-V for each definition
    emit_all_definitions(blk, sexpr_stage2_dir); // Emit S-expressions for each definition
    emit_all_invocations(blk, sexpr_stage2_dir); // Emit S-expressions for each invocation


    // Stage 3: Unit construction — flatten all logic into self-contained Invocation|Definition units
    unify_invocations(blk);            // Instantiate definition+invocation pairs as Units
    globalize_signal_names(blk); // Rewrite signal names to be globally unique per Unit (e.g., INV.AND.0.A)
    prepare_boundary_ports(blk); // Normalize boundary lists for each definition
    emit_all_units(blk, sexpr_stage3_dir);

    emit_all_units(blk, sexpr_stage4_dir);
                 // Emit final S-expr per Unit
  //  emit_all_units_to_spirv(blk, spirv_stage4_dir);     // Emit SPIR-V per Unit
  //  emit_all_units_to_verilog(blk, verilog_stage4_dir); // Emit Verilog per Unit

    // === Remaining analysis/evaluation ===
  //  propagate_intrablock_signals(blk); // Signal tracing (may be simplified now)
  //  print_signal_places(blk);          // Log resolved signals

    eval(blk); // Evaluate the system (simulation or codegen)

 
 
  //  emit_spirv_units(blk, sexpr_stage4_dir, spirv_stage4_dir);
    emit_spirv_asm_file(spirv_stage4_dir, spirv_asm_stage5_dir);

    dump_wiring(blk);
    dump_signals(blk);
}
