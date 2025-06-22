#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "gap.h"
#include "block.h"
#include "pubsub.h"
#include "mkrand.h"
#include "compiler.h"
#include "block_util.h"
#include "signal.h"
#include "signal_map.h"


int main(int argc, char *argv[]) {
    const char *inv_dir = NULL;
    const char *out_dir = NULL;
    int compile_mode = 0;

    // 🎛️ Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--inv") == 0 && i + 1 < argc) {
            inv_dir = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "--compile") == 0) {
            compile_mode = 1;
        }
    }

    if (compile_mode && (!inv_dir || !out_dir)) {
        fprintf(stderr, "❌ Missing required arguments: --inv and --output\n");
        return 1;
    }

    // 🛰️ Setup PubSub + Global Signal Table
    init_pubsub();
    SignalMap *global_signal_map = create_signal_map();

    // 🔧 Compile Invocation Block (if requested)
    Block blk = {0};
    if (compile_mode) {
        blk.psi = mkrand_generate_ipv6();
        compile_block(&blk, inv_dir, out_dir);
    }

    eval(&blk, global_signal_map);
    
    print_signal_map(global_signal_map);
    // 🧼 Cleanup
    destroy_signal_map(global_signal_map);
    cleanup_pubsub();
    return 0;
}
