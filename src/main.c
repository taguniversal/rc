#include "udp_send.h"
#include "mkrand.h"
#include "tinyosc.h"
#include "sqlite3.h"
#include "eval.h"
#include "osc.h"
#include "graph.h"
#include "log.h"
#include "wiring.h"
#include "rewrite_util.h"
#include "spirv.h"
#include "sexpr_parser.h"
#include "vulkan_driver.h"
#include "spirv_asm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <openssl/sha.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/socket.h>
#include <time.h>
#include <stdarg.h>
#include <dirent.h>

#include "config.h"

const char *default_schema =
    "CREATE TABLE IF NOT EXISTS triples ("
    "psi TEXT,"
    "subject TEXT,"
    "predicate TEXT,"
    "object TEXT,"
    "PRIMARY KEY (psi, subject, predicate, object)"
    ");";

char ipv6_address[INET6_ADDRSTRLEN];

volatile sig_atomic_t keep_running = 1;

void handle_signal(int sig)
{
    if (sig == SIGTERM || sig == SIGINT)
    {
        keep_running = 0;
    }
}

void daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0)
        exit(EXIT_FAILURE);
    if (pid > 0)
        exit(EXIT_SUCCESS); // Parent exits

    if (setsid() < 0)
        exit(EXIT_FAILURE);
    signal(SIGHUP, SIG_IGN);
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    pid = fork();
    if (pid < 0)
        exit(EXIT_FAILURE);
    if (pid > 0)
        exit(EXIT_SUCCESS);

    umask(0);
    chdir("/");
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int fd = open("/tmp/rc_server.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd != -1)
    {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
}

Block *start_block(void)
{
    Block *blk = (Block *)calloc(1, sizeof(Block));
    if (!blk)
    {
        LOG_ERROR("❌ Failed to allocate Block structure.");
        return NULL;
    }

    char *new_psi = new_block(); // new_block() comes from  mkrand lib
    if (!new_psi)
    {
        LOG_ERROR("❌ Failed to generate PSI for new Block.");
        free(blk);
        return NULL;
    }

    blk->psi = strdup(new_psi); // Save a copy inside Block
    blk->definitions = NULL;
    blk->invocations = NULL;

    LOG_INFO("✅ New Block started with PSI: %s", blk->psi);

    return blk;
}

char sexpr_out_dir[256];
char spirv_sexpr_dir[256];
char spirv_asm_dir[256];
char spirv_unified_dir[256];

void init_output_dirs(const char *base)
{
    snprintf(sexpr_out_dir, sizeof(sexpr_out_dir), "%s/sexpr", base);
    snprintf(spirv_sexpr_dir, sizeof(spirv_sexpr_dir), "%s/spirv_sexpr", base);
    snprintf(spirv_asm_dir, sizeof(spirv_asm_dir), "%s/spirv_asm", base);
    snprintf(spirv_unified_dir, sizeof(spirv_unified_dir), "%s/spirv_unified", base);

    mkdir(base, 0755); // harmless if it already exists
    mkdir(sexpr_out_dir, 0755);
    mkdir(spirv_sexpr_dir, 0755);
    mkdir(spirv_asm_dir, 0755);
    mkdir(spirv_unified_dir, 0755);

    // Don't mkdir(base) if meson already guarantees it exists.
}

int main(int argc, char *argv[])
{
    const char *state_dir = "./state";
    const char *inv_dir = "./inv";
    const char *out_dir = "build/out";
    int output_flag_provided = 0;

    char buffer[BUFFER_SIZE];
    sqlite3 *db = NULL;

    LOG_INFO("Reality Compiler CLI");
    LOG_INFO("🚀 Started with %d args:", argc);
    for (int i = 0; i < argc; ++i)
    {
        LOG_INFO("   argv[%d] = %s", i, argv[i]);
    }

    if (check_spirv_gpu_support())
    {
        LOG_INFO("🟢 SPIR-V compatible GPU detected — enabling GPU acceleration.");
    }
    else
    {
        LOG_WARN("🟠 No SPIR-V capable GPU found — running in CPU fallback mode.");
    }

    // 🧱 Genesis Block
    Block *active_block = start_block();

    LOG_INFO("Active Block: %s", active_block->psi);
    int compile_only = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--state") == 0 && i + 1 < argc)
        {
            state_dir = argv[++i];
        }
        else if (strcmp(argv[i], "--inv") == 0 && i + 1 < argc)
        {
            inv_dir = argv[++i];
            mkdir(inv_dir, 0755);
        }
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc)
        {
            out_dir = argv[++i];
            output_flag_provided = 1;
            init_output_dirs(out_dir);
        }
        else if (strcmp(argv[i], "--compile") == 0)
        {
            compile_only = 1;
        }
        else if (strcmp(argv[i], "--run_spirv") == 0)
        {
            if (!output_flag_provided)
            {
                init_output_dirs(out_dir); // ensure spirv_asm_dir is populated
            }
            return create_pipeline(spirv_asm_dir);
        }
    }

    LOG_INFO("Using state directory %s, invocation directory %s", state_dir, inv_dir);

    // 🗂️ Ensure state and spirv directories exist
    mkdir(state_dir, 0755);

    char db_path[256];
    snprintf(db_path, sizeof(db_path), "%s/db.sqlite3", state_dir);

    // 📂 Open SQLite DB
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK)
    {
        LOG_ERROR("Cannot open database: %s", sqlite3_errmsg(db));
        return 1;
    }
    LOG_INFO("SQLite initialized in %s/db.sqlite3.", state_dir);

    // 📜 Load schema
    char *errmsg = 0;
    if (sqlite3_exec(db, default_schema, 0, 0, &errmsg) != SQLITE_OK)
    {
        fprintf(stderr, "❌ Failed to load schema: %s", errmsg);
        sqlite3_free(errmsg);
    }
    else
    {
        LOG_INFO("✅ Schema loaded");
    }
    int parse_status = parse_block_from_sexpr(active_block, inv_dir);
    LOG_INFO("Parse status: %d", parse_status);

    // parse_block_from_xml(active_block, inv_dir);
    char graph_path[256];
    snprintf(graph_path, sizeof(graph_path), "%s/graph.json", out_dir);
    write_network_json(active_block, graph_path);

    if (compile_only)
    {
        LOG_INFO("🎯 Compile-only mode enabled");
        LOG_INFO("📂 Emitting definitions to: %s", out_dir);
        LOG_INFO("📂 Outputs:");
        LOG_INFO("   S-expressions: %s", sexpr_out_dir);
        LOG_INFO("   SPIR-V S-expr: %s", spirv_sexpr_dir);
        LOG_INFO("   SPIR-V Asm   : %s", spirv_asm_dir);
        parse_block_from_sexpr(active_block, sexpr_out_dir); // 1. Load structure
        rewrite_signals(active_block);                       // 2. Rewrite names to be globally unique
        flatten_signal_places(active_block);                 // 2.5. Collect all SourcePlace/DestinationPlace pointers
        print_signal_places(active_block);
        boundary_link_invocations_by_position(active_block); // 4. Wire by position (Invocation ↔ Definition)
        validate_invocation_wiring(active_block); // 5. Confirm everything is hooked up
        eval(active_block);
        emit_all_definitions(active_block, sexpr_out_dir); // 6. Emit definitions
        emit_all_invocations(active_block, sexpr_out_dir); // 7. Emit invocations
        spirv_parse_block(active_block, spirv_sexpr_dir);  // 8. Lower to SPIR-V
        char main_sexpr_path[256], main_spvasm_path[256];
        snprintf(main_sexpr_path, sizeof(main_sexpr_path), "%s/main.spvasm.sexpr", spirv_unified_dir);
        snprintf(main_spvasm_path, sizeof(main_spvasm_path), "%s/main.spvasm", spirv_asm_dir);

        emit_spirv_block(active_block, spirv_sexpr_dir, main_sexpr_path);
        emit_spirv_asm_file(main_sexpr_path, main_spvasm_path);

        dump_wiring(active_block);

        dump_signals(active_block);

        return 0;
    }

    // 🎛️ Handle flags
    if (argc > 1)
    {
        if (strcmp(argv[1], "--daemon") == 0)
        {
            LOG_INFO("🛠 Daemonizing...\n");
            daemonize();
            signal(SIGTERM, handle_signal);
            signal(SIGINT, handle_signal);
            LOG_INFO("🔍 Final DB Pointer before loop: %p\n", (void *)db);
            run_osc_listener(active_block, &keep_running);
            return 0;
        }

        if (strcmp(argv[1], "--debug") == 0)
        {
            signal(SIGTERM, handle_signal);
            signal(SIGINT, handle_signal);
            LOG_INFO("🧪 Debug Mode: Entering OSC Loop...\n");
            run_osc_listener(active_block, &keep_running);
            return 0;
        }
    }

    int len = tosc_writeMessage(buffer, BUFFER_SIZE, "/generate_ipv6", "");
    process_osc_message(active_block, buffer, len);

    sqlite3_close(db);

    return 0;
}
