#include "log.h"
#include "eval.h"
#include "eval_util.h"
#include "rewrite_util.h"
#include "sexpr_parser.h"
#include "spirv.h"
#include "spirv_asm.h"
#include "wiring.h"

void compile_block(Block *blk,
                   const char *inv_dir,
                   const char *out_dir,
                   const char *sexpr_out_dir,
                   const char *spirv_sexpr_dir,
                   const char *spirv_asm_dir,
                   const char *spirv_unified_dir)
{
    LOG_INFO("🎯 Compile-only mode enabled");
    LOG_INFO("📂 Emitting definitions to: %s", out_dir);
    LOG_INFO("📂 Outputs:");
    LOG_INFO("   S-expressions: %s", sexpr_out_dir);
    LOG_INFO("   SPIR-V S-expr: %s", spirv_sexpr_dir);
    LOG_INFO("   SPIR-V Asm   : %s", spirv_asm_dir);

    parse_block_from_sexpr(blk, inv_dir); // 1. Load structure
    rewrite_signals(blk);                 // 2. Rewrite names
    flatten_signal_places(blk);           // 2.5 Collect all signals
    wire_signal_propagation_by_name(blk); // 🧠 Link signals by name
    print_signal_places(blk);

    boundary_link_invocations_by_position(blk); // 4. Link Invocation ↔ Definition
    link_por_invocations_to_definitions(blk);
    validate_invocation_wiring(blk);            // 5. Ensure wiring is sane

    eval(blk); // 6. Run evaluation logic

    emit_all_definitions(blk, sexpr_out_dir); // 7. Emit outputs
    emit_all_invocations(blk, sexpr_out_dir);

    spirv_parse_block(blk, spirv_sexpr_dir); // 8. Lower to SPIR-V

    char main_sexpr_path[256], main_spvasm_path[256];
    snprintf(main_sexpr_path, sizeof(main_sexpr_path), "%s/main.spvasm.sexpr", spirv_unified_dir);
    snprintf(main_spvasm_path, sizeof(main_spvasm_path), "%s/main.spvasm", spirv_asm_dir);

    emit_spirv_block(blk, spirv_sexpr_dir, main_sexpr_path);
    emit_spirv_asm_file(main_sexpr_path, main_spvasm_path);

    dump_wiring(blk);
    dump_signals(blk);
}
