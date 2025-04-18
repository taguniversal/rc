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
#include <dirent.h>
#include "udp_send.h"  
#include "mkrand.h"
#include "tinyosc.h"
#include "cJSON.h"
#include "serd.h"
#include "sqlite3.h"
#include "rdf.h"
#include "eval.h"
#include "osc.h"
#include "graph.h"
#include "invocation.h"

#include <time.h>
#include <stdarg.h>

#include "log.h"


#define PSI_BLOCK_LEN 39
#define OSC_PORT_XMIT 4242
#define OSC_PORT_RECV 4243
#define BUFFER_SIZE 1024
#define IPV6_ADDR "fc00::1"

const char* default_schema = 
  "CREATE TABLE IF NOT EXISTS triples ("
  "psi TEXT,"
  "subject TEXT,"
  "predicate TEXT,"
  "object TEXT,"
  "PRIMARY KEY (psi, subject, predicate)"
  ");";


char ipv6_address[INET6_ADDRSTRLEN];

volatile sig_atomic_t keep_running = 1;

void handle_signal(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        keep_running = 0;
    }
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS); // Parent exits

    if (setsid() < 0) exit(EXIT_FAILURE);
    signal(SIGHUP, SIG_IGN);
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    chdir("/");
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int fd = open("/tmp/rc_server.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd != -1) {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
}


cJSON* run_poll_cycle(sqlite3* db, const char* block) {
    // Create the root JSON object
    cJSON* root = cJSON_CreateObject();

    // Timestamp
    time_t now = time(NULL);
    struct tm* tm_info = gmtime(&now);
    char iso_timestamp[32];
    strftime(iso_timestamp, sizeof(iso_timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    cJSON_AddStringToObject(root, "timestamp", iso_timestamp);

    // Placeholder: executed invocations array
    cJSON* executed = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "executed", executed);

    // Placeholder: live values object
    cJSON* live = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "live_values", live);

    // TODO: Add logic here to scan triples and update these sections
    return root;
  
}


int main(int argc, char *argv[]) {
    const char* state_dir = "./state";
    const char* inv_dir = "./inv";
    char* block = "INVALID";
    char buffer[BUFFER_SIZE];
    sqlite3 *db;
    LOG_INFO("Reality Compiler CLI\n");

    // 🧱 Genesis Block
    block = new_block();
    LOG_INFO("Genesis Block: %s\n", block);

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--state") == 0 && i + 1 < argc) {
            state_dir = argv[++i];
        } else if (strcmp(argv[i], "--inv") == 0 && i + 1 < argc) {
            inv_dir = argv[++i];
            // 🗂️ Ensure inv directory exists
            if (inv_dir) {
                mkdir(inv_dir, 0755);
            }
        }
    }
    LOG_INFO("Using state directory %s, invocation directory %s\n", state_dir, inv_dir);

    // 🗂️ Ensure state directory exists
    mkdir(state_dir, 0755);
    char db_path[256];
    snprintf(db_path, sizeof(db_path), "%s/db.sqlite3", state_dir);

    // 📂 Open SQLite DB
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Cannot open database: %s\n", sqlite3_errmsg(db));
        return 1; 
    }
    LOG_INFO("SQLite initialized in %s/db.sqlite3.\n", state_dir);

    // 📜 Load schema
    char *errmsg = 0;
    if (sqlite3_exec(db, default_schema, 0, 0, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "❌ Failed to load schema: %s\n", errmsg);
        sqlite3_free(errmsg);
    } else {
        LOG_INFO("✅ Schema loaded\n");
    }

    // 📥 Load RDF UI triples and Invocation IO mappings
   // load_rdf_from_dir("ui", db, block);
    map_io(db, block, inv_dir);
    eval(db, block);


       // 🎛️ Handle flags
       if (argc > 1) {
        if (strcmp(argv[1], "--daemon") == 0) {
            LOG_INFO("🛠 Daemonizing...\n");
            daemonize();
            signal(SIGTERM, handle_signal);
            signal(SIGINT, handle_signal);
            LOG_INFO("🔍 Final DB Pointer before loop: %p\n", db);
            run_osc_listener(db, block, &keep_running);
            return 0;
        }

        if (strcmp(argv[1], "--debug") == 0) {
            signal(SIGTERM, handle_signal);
            signal(SIGINT, handle_signal);
            LOG_INFO("🧪 Debug Mode: Entering OSC Loop...\n");
            run_osc_listener(db, block, &keep_running);
            return 0;
        }

        if (strcmp(argv[1], "--export-graph") == 0) {
            export_dot(db, "graph.dot");
            return 0;
        }


        if (strcmp(argv[1], "--osc") == 0 && argc >= 3) {
            const char* json_input = argv[2];
            char buffer[2048] = {0};
        
            // 🔁 Read from stdin if `-` is passed
            if (strcmp(json_input, "-") == 0) {
                size_t total_read = fread(buffer, 1, sizeof(buffer) - 1, stdin);
                buffer[total_read] = '\0';
                json_input = buffer;
            }
        
            cJSON* json = cJSON_Parse(json_input);
            if (!json) {
                fprintf(stderr, "❌ Failed to parse JSON: %s\n", cJSON_GetErrorPtr());
                exit(1);
            }
        
            const cJSON* path = cJSON_GetObjectItemCaseSensitive(json, "path");
            const cJSON* value = cJSON_GetObjectItemCaseSensitive(json, "value");
        
            if (cJSON_IsString(path) && cJSON_IsNumber(value)) {
                send_osc_response(path->valuestring, value->valuedouble);
            } else {
                fprintf(stderr, "❌ Invalid JSON structure.\n");
            }
        
            cJSON_Delete(json);
            exit(0);
        }


        if (strcmp(argv[1], "--poll") == 0) {
            LOG_INFO("🔍 Starting poll cycle...");
            cJSON* report = run_poll_cycle(db, block);
        
            if (report) {
                LOG_INFO("✅ Poll cycle returned successfully.");
                char* out = cJSON_Print(report);
                fprintf(stdout, "%s\n", out);  // same result, but more control
                free(out);
                cJSON_Delete(report);
            } else {
                LOG_ERROR("❌ Polling cycle failed: report was NULL.");
            }
        
            sqlite3_close(db);
            if (block) free(block);
            return 0;
        }
    }

    // 🧪 Default test block
    const char *json_string = "{\"name\": \"Reality Compiler\"}";
    cJSON *json = cJSON_Parse(json_string);
    if (json) {
        cJSON *name = cJSON_GetObjectItemCaseSensitive(json, "name");
        if (cJSON_IsString(name) && name->valuestring != NULL) {
            LOG_INFO("JSON Parsed: Name = %s\n", name->valuestring);
        }
        cJSON_Delete(json);
    }

    LOG_INFO("Serd initialized.\n");

    int len = tosc_writeMessage(buffer, BUFFER_SIZE, "/generate_ipv6", "");
    process_osc_message(db, buffer, block, len);

    sqlite3_close(db);
    if (block) free(block);

    return 0;
}

