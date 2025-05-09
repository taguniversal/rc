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
#include "udp_send.h"
#include "mkrand.h"
#include "tinyosc.h"
#include "sqlite3.h"
#include "eval.h"
#include "osc.h"
#include "graph.h"
#include "invocation.h"
#include "log.h"
#include "wiring.h"
#include "spirv.h"

#define PSI_BLOCK_LEN 39
#define OSC_PORT_XMIT 4242
#define OSC_PORT_RECV 4243
#define BUFFER_SIZE 1024
#define IPV6_ADDR "fc00::1"

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

void daemonize()
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

int main(int argc, char *argv[])
{
    const char *state_dir = "./state";
    const char *inv_dir = "./inv";
    const char *spirv_dir = "./spirv";
    char buffer[BUFFER_SIZE];
    sqlite3 *db = NULL;

    LOG_INFO("Reality Compiler CLI\n");
    // 🧱 Genesis Block
    Block *active_block = start_block();

    LOG_INFO("Active Block: %s\n", active_block->psi);

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--state") == 0 && i + 1 < argc)
        {
            state_dir = argv[++i];
        }
        else if (strcmp(argv[i], "--inv") == 0 && i + 1 < argc)
        {
            inv_dir = argv[++i];
            // 🗂️ Ensure inv directory exists
            if (inv_dir)
            {
                mkdir(inv_dir, 0755);
            }
        }
    }
    LOG_INFO("Using state directory %s, invocation directory %s\n", state_dir, inv_dir);

    int compile_only = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--spirv_dir") == 0 && i + 1 < argc)
        {
            spirv_dir = argv[++i];
        }
        else if (strcmp(argv[i], "--compile") == 0)
        {
            compile_only = 1;
        }
    }

    // 🗂️ Ensure state and spirv directories exist
    mkdir(state_dir, 0755);
    mkdir(spirv_dir, 0755);
    char db_path[256];
    snprintf(db_path, sizeof(db_path), "%s/db.sqlite3", state_dir);

    // 📂 Open SQLite DB
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK)
    {
        LOG_ERROR("Cannot open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    LOG_INFO("SQLite initialized in %s/db.sqlite3.\n", state_dir);

    // 📜 Load schema
    char *errmsg = 0;
    if (sqlite3_exec(db, default_schema, 0, 0, &errmsg) != SQLITE_OK)
    {
        fprintf(stderr, "❌ Failed to load schema: %s\n", errmsg);
        sqlite3_free(errmsg);
    }
    else
    {
        LOG_INFO("✅ Schema loaded\n");
    }

    DefinitionLibrary *deflib = create_definition_library();
    if (validate_snippets(inv_dir) != 0)
    {
        fprintf(stderr, "❌ Validation failed");
    }
    else
    {
        LOG_INFO("✅ Snippets validated.\n");
    }

    parse_block_from_xml(active_block, inv_dir);
    spirv_parse_block(active_block, spirv_dir);
    link_invocations(active_block);
    write_network_json(active_block, "./graph.json");


    if (compile_only)
    {
        LOG_INFO("🎯 SPIR-V generation only mode: emitting to %s", spirv_dir);
        spirv_parse_block(active_block, spirv_dir);
        return 0;
    }
    eval(active_block);
    dump_wiring(active_block);

    // 🎛️ Handle flags
    if (argc > 1)
    {
        if (strcmp(argv[1], "--daemon") == 0)
        {
            LOG_INFO("🛠 Daemonizing...\n");
            daemonize();
            signal(SIGTERM, handle_signal);
            signal(SIGINT, handle_signal);
            LOG_INFO("🔍 Final DB Pointer before loop: %p\n", db);
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

        if (strcmp(argv[1], "--export-graph") == 0)
        {
            write_network_json(active_block, "./graph.json");
            return 0;
        }
    }

    int len = tosc_writeMessage(buffer, BUFFER_SIZE, "/generate_ipv6", "");
    process_osc_message(active_block, buffer, len);

    sqlite3_close(db);

    return 0;
}
