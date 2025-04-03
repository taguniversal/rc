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
#include "udp_send.h"  // You’ll need to create or adapt this


#include "mkrand.h"
#include "tinyosc.h"
#include "cJSON.h"
#include "serd.h"
#include "sqlite3.h"
#include "rdf.h"
#include "eval.h"

#include <time.h>
#include <stdarg.h>
#include <unistd.h>
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
  "object TEXT"
  ");";


sqlite3 *db;
char* block;
char ipv6_address[INET6_ADDRSTRLEN];
char buffer[BUFFER_SIZE];
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

void generate_ipv6_address(const uint8_t *pubkey, char *ipv6_str) {
    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256(pubkey, 32, hash);

    struct in6_addr addr;
    memset(&addr, 0, sizeof(addr));
    addr.s6_addr[0] = 0xFC;
    memcpy(&addr.s6_addr[1], hash, 6);

    uint8_t full_rand[16];
    mkrand_generate_ipv6(pubkey, full_rand);
    memcpy(&addr.s6_addr[8], &full_rand[8], 8);

    inet_ntop(AF_INET6, &addr, ipv6_str, INET6_ADDRSTRLEN);
}

void send_osc_response(const char *address, float value) {
    int sockfd;
    struct sockaddr_in touchosc_addr;

    // 1. Emit JSON response to stdout
    printf("{\"path\": \"%s\", \"value\": %.6f}\n", address, value);
    fflush(stdout);  // <---- THIS
    // 2. Send OSC UDP message (same as before)
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        return;
    }

    memset(&touchosc_addr, 0, sizeof(touchosc_addr));
    touchosc_addr.sin_family = AF_INET;
    touchosc_addr.sin_port = htons(OSC_PORT_RECV);
    touchosc_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    char msg[BUFFER_SIZE];
    int len = tosc_writeMessage(msg, BUFFER_SIZE, address, "f", value);
    
    sendto(sockfd, msg, len, 0, (struct sockaddr *)&touchosc_addr, sizeof(touchosc_addr));
    close(sockfd);
}

void send_osc_response_str(const char *address, const char *str) {
    int sockfd;
    struct sockaddr_in dest_addr;

    // Emit JSON to stdout
    printf("{\"path\": \"%s\", \"value\": \"%s\"}\n", address, str);
    fflush(stdout);

    // Create socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        return;
    }

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(OSC_PORT_RECV);
    dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    char msg[BUFFER_SIZE];
    int len = tosc_writeMessage(msg, BUFFER_SIZE, address, "s", str);

    sendto(sockfd, msg, len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    close(sockfd);
}

char* enrich(const char* osc_path) {
    // Strip leading '/' if present
    const char* base_name = (osc_path[0] == '/') ? osc_path + 1 : osc_path;

    // Construct file path (e.g., "ui/radar1.json")
    char filename[128];
    snprintf(filename, sizeof(filename), "ui/%s.json", base_name);

    FILE* file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "❌ enrich(): Could not open %s\n", filename);
        return strdup("");  // Return empty string if not found
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    rewind(file);

    // Allocate buffer and read
    char* content = malloc(len + 1);
    fread(content, 1, len, file);
    content[len] = '\0';
    fclose(file);

    return content;
}


const char* lookup_name_by_osc_path(sqlite3* db, const char* osc_path) {
    static char name[128];
    sqlite3_stmt* stmt;

    // Step 1: Find subject where inv:from = osc_path
    const char *sql = "SELECT subject FROM triples WHERE predicate = 'inv:from' AND object = ? LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;

    sqlite3_bind_text(stmt, 1, osc_path, -1, SQLITE_STATIC);

    char subject[128];
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* subj = sqlite3_column_text(stmt, 0);
        snprintf(subject, sizeof(subject), "%s", subj);
        sqlite3_finalize(stmt);
    } else {
        sqlite3_finalize(stmt);
        return NULL;
    }

    // Step 2: Look up inv:name for that subject
    const char *sql2 = "SELECT object FROM triples WHERE subject = ? AND predicate = 'inv:name' LIMIT 1;";
    if (sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL) != SQLITE_OK) return NULL;

    sqlite3_bind_text(stmt, 1, subject, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* val = sqlite3_column_text(stmt, 0);
        snprintf(name, sizeof(name), "%s", val);
        sqlite3_finalize(stmt);
        return name;
    }

    sqlite3_finalize(stmt);
    return NULL;
}

int lookup_destinations_for_name(sqlite3* db, const char* name, char dests[][128], int max_dests) {
    sqlite3_stmt* stmt;
    const char *sql = "SELECT DISTINCT s.subject FROM triples s \
                   WHERE s.predicate = 'inv:name' AND s.object = ?;";
    int count = 0;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_dests) {
        const char* subject = (const char*)sqlite3_column_text(stmt, 0);

        // Verify it's a DestinationPlace
        sqlite3_stmt* stmt2;
        const char* check_type = "SELECT 1 FROM triples WHERE subject = ? AND predicate = 'rdf:type' AND object = 'inv:DestinationPlace';";
        if (sqlite3_prepare_v2(db, check_type, -1, &stmt2, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt2, 1, subject, -1, SQLITE_STATIC);
            if (sqlite3_step(stmt2) == SQLITE_ROW) {
                strncpy(dests[count], subject, 128);
                count++;
            }
            sqlite3_finalize(stmt2);
        }
    }

    sqlite3_finalize(stmt);
    return count;
}


void process_osc_message(const char *buffer, int len) {
    tosc_message osc;
    LOG_INFO("📡 Raw OSC Message: %s\n", buffer);

    sqlite3_int64 mem_used = sqlite3_memory_used();
    sqlite3_int64 mem_high = sqlite3_memory_highwater(1);
    LOG_INFO("SQLite Memory Used: %lld bytes, Highwater: %lld bytes\n", mem_used, mem_high);

    // Generate ISO 8601 timestamp
    char iso_timestamp[32];
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);  // Use gmtime for UTC (or localtime for local tz)
    strftime(iso_timestamp, sizeof(iso_timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);

    LOG_INFO("⏱️  Timestamp: %s\n", iso_timestamp);
    
    if (tosc_parseMessage(&osc, (char *)buffer, len) == 0) {
        LOG_INFO("📡 Parsed OSC Address: %s\n", osc.buffer);
        if (osc.format && *osc.format)
            LOG_INFO("📡 Extracted OSC Format: %s\n", osc.format);
        else
            LOG_INFO("⚠️ Warning: OSC format string is empty!\n");

        if (osc.marker)
            LOG_INFO("📡 OSC Payload (raw): %s\n", osc.marker);
        else
            LOG_INFO("⚠️ Warning: No payload found in OSC message!\n");

        if (strcmp(osc.buffer, "/generate_ipv6") == 0) {
            char ipv6_str[INET6_ADDRSTRLEN];
            uint8_t pubkey[32] = {0x01, 0x02, 0x03};
            generate_ipv6_address(pubkey, ipv6_str);
            LOG_INFO("✅ Generated IPv6: %s\n", ipv6_str);
        }

        if (strcmp(osc.buffer, "/poll") == 0) {
            LOG_INFO("📡 Received /poll\n");
        
            const char *json = "{\"status\":\"ok\"}";
            send_osc_response_str("/poll_response", json);
        }
        

        if (strcmp(osc.buffer, "/button1") == 0) {
            float val = 0.0;
            if (osc.format[0] == 'f') {   
                val = tosc_getNextFloat(&osc);
                LOG_INFO("🟢 Button 1 sent value: %f\n", val);
            }
        
            // Create timestamp
            char iso_timestamp[32];
            time_t now = time(NULL);
            struct tm *tm_info = gmtime(&now);
            strftime(iso_timestamp, sizeof(iso_timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
        
            LOG_INFO("🟢 Button 1 pressed! Triggering event...\n");
            LOG_INFO("⏱️  Timestamp: %s\n", iso_timestamp);
        
            // Convert value to string for RDF storage
            char val_str[32];
            snprintf(val_str, sizeof(val_str), "%f", val);
        
            // Save RDF triples
            insert_triple(db, block, "/button1", "context", enrich("/button1"));
            insert_triple(db, block, "/button1", "pressedValue", val_str);
            insert_triple(db, block, "/button1", "timestamp", iso_timestamp);
        
            // Respond via OSC (for TouchOSC or other loopback use)
            send_osc_response("/button1_response", val + 1.0);
        }
        
        
        if (strncmp(osc.buffer, "/grid1/", 7) == 0) {
            int row = atoi(osc.buffer + 7);  // Extract row number from OSC address
            float value = tosc_getNextFloat(&osc);
        
            // Create timestamp
            char iso_timestamp[32];
            time_t now = time(NULL);
            struct tm *tm_info = gmtime(&now);
            strftime(iso_timestamp, sizeof(iso_timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
        
            LOG_INFO("🟢 Grid Update: Row %d = %f\n", row, value);
            LOG_INFO("⏱️  Timestamp: %s\n", iso_timestamp);
        
            // Convert value to string
            char val_str[32];
            snprintf(val_str, sizeof(val_str), "%f", value);
        
            // Convert row to string (e.g., "/grid1/3")
            char subject[32];
            snprintf(subject, sizeof(subject), "/grid1/%d", row);
        
            // Save RDF triples
            insert_triple(db, block, "/grid1", "context", enrich("/grid1"));
            insert_triple(db, block, subject, "rowValue", val_str);
            insert_triple(db, block, subject, "timestamp", iso_timestamp);
        }
        

        if (strcmp(osc.buffer, "/radar1") == 0) {
            float x = tosc_getNextFloat(&osc);
            float y = tosc_getNextFloat(&osc);
        
            // Create timestamp
            char iso_timestamp[32];
            time_t now = time(NULL);
            struct tm *tm_info = gmtime(&now);
            strftime(iso_timestamp, sizeof(iso_timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
        
            LOG_INFO("📡 Radar Update: X = %f, Y = %f\n", x, y);
            LOG_INFO("⏱️  Timestamp: %s\n", iso_timestamp);
        
            // Compose strings
            char x_str[32], y_str[32];
            snprintf(x_str, sizeof(x_str), "%f", x);
            snprintf(y_str, sizeof(y_str), "%f", y);
            insert_triple(db, block, "/radar1", "context", enrich("/radar1"));
            // Save RDF triples (requires db and block to be global or passed in)
            insert_triple(db, block, "/radar1", "hasX", x_str);
            insert_triple(db, block, "/radar1", "hasY", y_str);
            insert_triple(db, block, "/radar1", "timestamp", iso_timestamp);

          //  cJSON *series = query_time_series(db, "/radar1", "hasX");
          //  char *json_str = cJSON_Print(series);
          //  printf("📈 Time Series JSON: %s\n", json_str);
          //  free(json_str);
          //  cJSON_Delete(series);

        }
        

        if (strcmp(osc.buffer, "/pager1") == 0) {
            int page = tosc_getNextInt32(&osc);
        
            // Create timestamp
            char iso_timestamp[32];
            time_t now = time(NULL);
            struct tm *tm_info = gmtime(&now);
            strftime(iso_timestamp, sizeof(iso_timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
        
            LOG_INFO("📡 Pager Activated: Page %d\n", page);
            LOG_INFO("⏱️  Timestamp: %s\n", iso_timestamp);
        
            // Convert page to string for RDF storage
            char page_str[16];
            snprintf(page_str, sizeof(page_str), "%d", page);
        
            // Save RDF triples
            insert_triple(db, block, "/pager1", "context", enrich("/pager1"));
            insert_triple(db, block, "/pager1", "selectedPage", page_str);
            insert_triple(db, block, "/pager1", "timestamp", iso_timestamp);
        }
        

        if (strcmp(osc.buffer, "/encoder1") == 0) {
            float value = tosc_getNextFloat(&osc);
        
            // Create timestamp
            char iso_timestamp[32];
            time_t now = time(NULL);
            struct tm *tm_info = gmtime(&now);
            strftime(iso_timestamp, sizeof(iso_timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
        
            LOG_INFO("🔄 Encoder Moved: %f\n", value);
            LOG_INFO("⏱️  Timestamp: %s\n", iso_timestamp);
        
            // Convert value to string for RDF storage
            char value_str[32];
            snprintf(value_str, sizeof(value_str), "%f", value);
        
            // Save RDF triples
            insert_triple(db, block, "/encoder1", "context", enrich("/encoder1"));
            insert_triple(db, block, "/encoder1", "rotatedTo", value_str);
            insert_triple(db, block, "/encoder1", "timestamp", iso_timestamp);
        }
        

        if (strcmp(osc.buffer, "/radio1") == 0) {
            int selection = tosc_getNextInt32(&osc);
        
            // Create timestamp
            char iso_timestamp[32];
            time_t now = time(NULL);
            struct tm *tm_info = gmtime(&now);
            strftime(iso_timestamp, sizeof(iso_timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
        
            LOG_INFO("📡 Radio Button Selected: Option %d\n", selection);
            LOG_INFO("⏱️  Timestamp: %s\n", iso_timestamp);
        
            // Convert selection to string for RDF storage
            char sel_str[16];
            snprintf(sel_str, sizeof(sel_str), "%d", selection);
        
            // Save RDF triples
            insert_triple(db, block, "/radio1", "context", enrich("/radio1"));
            insert_triple(db, block, "/radio1", "selectedOption", sel_str);
            insert_triple(db, block, "/radio1", "timestamp", iso_timestamp);
        }
        

        if (strcmp(osc.buffer, "/xy1") == 0) {
            float x = tosc_getNextFloat(&osc);
            float y = tosc_getNextFloat(&osc);
        
            // Create timestamp
            char iso_timestamp[32];
            time_t now = time(NULL);
            struct tm *tm_info = gmtime(&now);
            strftime(iso_timestamp, sizeof(iso_timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
        
            LOG_INFO("🕹️ XY Pad Moved: X = %f, Y = %f\n", x, y);
            LOG_INFO("⏱️  Timestamp: %s\n", iso_timestamp);
        
            // Convert values to strings for RDF storage
            char x_str[32], y_str[32];
            snprintf(x_str, sizeof(x_str), "%f", x);
            snprintf(y_str, sizeof(y_str), "%f", y);
        
            // Save RDF triples
            insert_triple(db, block, "/xy1", "context", enrich("/xy1"));
            insert_triple(db, block, "/xy1", "positionX", x_str);
            insert_triple(db, block, "/xy1", "positionY", y_str);
            insert_triple(db, block, "/xy1", "timestamp", iso_timestamp);
        }
        

        if (strcmp(osc.buffer, "/fader5") == 0) {
            float value = tosc_getNextFloat(&osc);
        
            // Create timestamp
            char iso_timestamp[32];
            time_t now = time(NULL);
            struct tm *tm_info = gmtime(&now);
            strftime(iso_timestamp, sizeof(iso_timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
        
            LOG_INFO("🎚️ Fader 5 Adjusted: %f\n", value);
            LOG_INFO("⏱️  Timestamp: %s\n", iso_timestamp);
        
            // Convert value to string for RDF storage
            char value_str[32];
            snprintf(value_str, sizeof(value_str), "%f", value);
        
            // Save RDF triples
            insert_triple(db, block, "/fader5", "context", enrich("/fader5"));
            insert_triple(db, block, "/fader5", "faderValue", value_str);
            insert_triple(db, block, "/fader5", "timestamp", iso_timestamp);
        }

    } else {
        printf("❌ Error parsing OSC message\n");
    }
}



char* load_file(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    rewind(file);

    char* buffer = malloc(len + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    fread(buffer, 1, len, file);
    buffer[len] = '\0';
    fclose(file);
    return buffer;
}

bool is_type(cJSON* root, const char* type) {
    cJSON* t = cJSON_GetObjectItem(root, "@type");
    if (!t) return false;

    if (cJSON_IsArray(t)) {
        cJSON* item = NULL;
        cJSON_ArrayForEach(item, t) {
            if (cJSON_IsString(item) && strcmp(item->valuestring, type) == 0) {
                return true;
            }
        }
    } else if (cJSON_IsString(t)) {
        return strcmp(t->valuestring, type) == 0;
    }

    return false;
}

const char* get_value(cJSON* root, const char* key) {
    cJSON* val = cJSON_GetObjectItem(root, key);
    if (val && cJSON_IsString(val)) {
        return val->valuestring;
    }
    return NULL;
}

const char* anon_id(void) {
   return "0";
}

void map_io(sqlite3* db, const char* inv_dir) {
    DIR* dir = opendir(inv_dir);
    struct dirent* entry;
    bool matched = false;
    LOG_INFO("🔍 map_io starting. in %s\n", inv_dir);

    if (!dir) {
        LOG_ERROR("❌ '%s' directory not found. Skipping map_io.", inv_dir);
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".json")) {
            char path[256];
            snprintf(path, sizeof(path), "inv/%s", entry->d_name);
            LOG_INFO("Loading %s\n", path);
            char* json = load_file(path);
            if (!json) continue;

            cJSON* root = cJSON_Parse(json);
            if (!root) {
                free(json);
                continue;
            }

            if (is_type(root, "inv:SourcePlace")) {
                matched = true;
            
                const char* name    = get_value(root, "inv:name");
                const char* content = get_value(root, "inv:content");
                const char* id      = get_value(root, "@id");
            
                if (id) {
                    LOG_INFO("🟢 SourcePlace: name=%s id=%s\n", name ? name : "(unnamed)", id);
                    insert_triple(db, block, id, "rdf:type", "inv:SourcePlace");
            
                    if (name)
                        insert_triple(db, block, id, "inv:name", name);
            
                    if (content)
                        insert_triple(db, block, id, "inv:content", content);
            
                    insert_triple(db, block, id, "context", json);
                }
            }
            

            if (is_type(root, "inv:DestinationPlace")) {
                matched = true;
            
                const char* name    = get_value(root, "inv:name");
                const char* content = get_value(root, "inv:content");  // Optional if you're writing output
                const char* id      = get_value(root, "@id");
            
                if (id) {
                    LOG_INFO("🔵 DestinationPlace: name=%s id=%s\n", name ? name : "(unnamed)", id);
                    insert_triple(db, block, id, "rdf:type", "inv:DestinationPlace");
            
                    if (name)
                        insert_triple(db, block, id, "inv:name", name);
            
                    if (content)
                        insert_triple(db, block, id, "inv:content", content);
            
                    insert_triple(db, block, id, "context", json);
                }
            }
            

            if (is_type(root, "inv:Expression")) {
                matched = true;
                const char* name = get_value(root, "inv:name");
                const char* id = get_value(root, "@id");
                if (!id) id = anon_id();

                LOG_INFO("📘 Expression: name=%s id=%s\n", name, id);
                insert_triple(db, block, id, "rdf:type", "inv:Expression");
                if (name) insert_triple(db, block, id, "inv:name", name);
                insert_triple(db, block, id, "context", json);

                // --- NEW: source_list ---
                cJSON* src_list = cJSON_GetObjectItem(root, "inv:source_list");
                if (src_list && cJSON_IsArray(src_list)) {
                    for (int i = 0; i < cJSON_GetArraySize(src_list); i++) {
                        cJSON* src = cJSON_GetArrayItem(src_list, i);
                        const char* from = get_value(src, "@id");
                        const char* name = get_value(src, "inv:name");
                        if (from) {
                            LOG_INFO("🟢 [Expression] SourcePlace: name=%s from=%s\n", name ? name : "(unnamed)", from);
                            insert_triple(db, block, from, "rdf:type", "inv:SourcePlace");
                            if (name) insert_triple(db, block, from, "inv:name", name);
                        }
                    }
                }

                // --- NEW: destination_list ---
                cJSON* dst_list = cJSON_GetObjectItem(root, "inv:destination_list");
                if (dst_list && cJSON_IsArray(dst_list)) {
                    for (int i = 0; i < cJSON_GetArraySize(dst_list); i++) {
                        cJSON* dst = cJSON_GetArrayItem(dst_list, i);
                        const char* to = get_value(dst, "@id");
                        const char* name = get_value(dst, "inv:name");
                        const char* to_ref = get_value(dst, "inv:to");
                        if (to) {
                            LOG_INFO("🔵 [Expression] DestinationPlace: name=%s to=%s\n", name ? name : "(unnamed)", to_ref ? to_ref : to);
                            insert_triple(db, block, to, "rdf:type", "inv:DestinationPlace");
                            if (name) insert_triple(db, block, to, "inv:name", name);
                            if (to_ref) insert_triple(db, block, to, "inv:to", to_ref);
                        }
                    }
                }

                // --- NEW: embedded PlaceOfResolution ---
                cJSON* por = cJSON_GetObjectItem(root, "inv:place_of_resolution");
                if (por && cJSON_IsObject(por)) {
                    const char* por_id = get_value(por, "@id");
                    if (!por_id) por_id = anon_id();
                    LOG_INFO("🧠 [Expression] PlaceOfResolution: id=%s\n", por_id);
                    insert_triple(db, block, por_id, "rdf:type", "inv:PlaceOfResolution");

                    cJSON* fragments = cJSON_GetObjectItem(por, "inv:hasExpressionFragment");
                    if (fragments && cJSON_IsArray(fragments)) {
                        int count = cJSON_GetArraySize(fragments);
                        for (int i = 0; i < count; i++) {
                            cJSON* fragment = cJSON_GetArrayItem(fragments, i);
                            if (!fragment || !cJSON_IsObject(fragment)) continue;

                            const char* frag_id = get_value(fragment, "@id");
                            if (!frag_id) frag_id = anon_id();

                            LOG_INFO("🔩 ExpressionFragment: %s\n", frag_id);
                            insert_triple(db, block, frag_id, "rdf:type", "inv:ExpressionFragment");
                            insert_triple(db, block, por_id, "inv:hasExpressionFragment", frag_id);

                            const char* target = get_value(fragment, "inv:targetPlace");
                            const char* def = get_value(fragment, "inv:invokesDefinition");
                            const char* args = get_value(fragment, "inv:invocationArguments");

                            if (target) insert_triple(db, block, frag_id, "inv:targetPlace", target);
                            if (def) insert_triple(db, block, frag_id, "inv:invokesDefinition", def);
                            
                            if (args) insert_triple(db, block, frag_id, "inv:invocationArguments", args);
                        }
                    }
                }
            }

            

            free(json);
            if (!matched) {
                const char* type = get_value(root, "@type");
                if (type) {
                    LOG_WARN("⚠️ Unrecognized type: %s in file %s\n", type, entry->d_name);
                } else {
                    LOG_WARN("⚠️ No @type found in file %s\n", entry->d_name);
                }
            }
            
            cJSON_Delete(root);
        }
    }

    closedir(dir);
}


void process_osc_response(char *buffer, int len) {
    tosc_message osc;
    tosc_parseMessage(&osc, buffer, len);

    const char *addr = tosc_getAddress(&osc);
    const char *name = lookup_name_by_osc_path(db, addr);
    if (!name) return;

    LOG_INFO("Found source %s\n", name);

    char dests[10][128];  // Support up to 10 fan-out targets
    int num = lookup_destinations_for_name(db, name, dests, 10);
    if (num == 0) return;

    float v = tosc_getNextFloat(&osc);

    for (int i = 0; i < num; i++) {
        LOG_INFO("🔁 Fanout %s → %s (via name: %s) with value: %f\n", addr, dests[i], name, v);
        send_osc_response(dests[i], v);
    }
}



void run_osc_listener() {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t len;
    ssize_t n;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(OSC_PORT_XMIT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    struct timeval timeout;
    timeout.tv_sec = 1;  // check for signals every 1 second
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    LOG_INFO("🎧 Listening for OSC on port %d...\n", OSC_PORT_XMIT);

    while (keep_running) {
        len = sizeof(cliaddr);
        n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&cliaddr, &len);
        if (n > 0) {
            process_osc_message(buffer, n);
            
            process_osc_response(buffer, n);
        }
        // else: timeout occurred, just loop back and check keep_running
    }

    LOG_INFO("👋 Shutting down OSC listener.\n");
    close(sockfd);
}

void export_dot(sqlite3 *db, const char *filename) {
    FILE *dot = fopen(filename, "w");
    if (!dot) {
        perror("fopen for dot");
        return;
    }

    fprintf(dot, "digraph G {\n");
    fprintf(dot, "  rankdir=LR;\n");
    fprintf(dot, "  node [shape=ellipse, fontname=\"Helvetica\"];\n");

    sqlite3_stmt *stmt;
    const char *query = "SELECT subject, predicate, object FROM triples WHERE predicate = 'rdf:type';";

    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *subj = (const char *)sqlite3_column_text(stmt, 0);
            const char *type = (const char *)sqlite3_column_text(stmt, 2);

            const char *color = strstr(type, "SourcePlace") ? "green" :
                                strstr(type, "DestinationPlace") ? "blue" : "gray";

            fprintf(dot, "  \"%s\" [style=filled, fillcolor=%s];\n", subj, color);
        }
        sqlite3_finalize(stmt);
    }

    // Draw matching edges by name (basic string equality match)
    const char *link_query = "SELECT DISTINCT s1.subject, s2.subject "
                             "FROM triples s1, triples s2 "
                             "WHERE s1.predicate = 'rdf:type' AND s1.object = 'inv:SourcePlace' "
                             "AND s2.predicate = 'rdf:type' AND s2.object = 'inv:DestinationPlace' "
                             "AND s1.subject = s2.subject;";

    if (sqlite3_prepare_v2(db, link_query, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *from = (const char *)sqlite3_column_text(stmt, 0);
            const char *to = (const char *)sqlite3_column_text(stmt, 1);
            fprintf(dot, "  \"%s\" -> \"%s\";\n", from, to);
        }
        sqlite3_finalize(stmt);
    }

    fprintf(dot, "}\n");
    fclose(dot);

    printf("✅ DOT file written to %s\n", filename);
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

    // Example: dummy value
    cJSON_AddNumberToObject(live, "/grid1/2", 0.521434);
    cJSON_AddNumberToObject(live, "/fader5", 0.1123);

    return root;
}


int main(int argc, char *argv[]) {
    const char* state_dir = "./state";
    const char* inv_dir = "./inv";
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
    load_rdf_from_dir("ui", db, block);
    map_io(db, inv_dir);
    eval(db);


       // 🎛️ Handle flags
       if (argc > 1) {
        if (strcmp(argv[1], "--daemon") == 0) {
            LOG_INFO("🛠 Daemonizing...\n");
            daemonize();
            signal(SIGTERM, handle_signal);
            signal(SIGINT, handle_signal);
            LOG_INFO("🔍 Final DB Pointer before loop: %p\n", db);
            run_osc_listener();
            return 0;
        }

        if (strcmp(argv[1], "--debug") == 0) {
            signal(SIGTERM, handle_signal);
            signal(SIGINT, handle_signal);
            LOG_INFO("🧪 Debug Mode: Entering OSC Loop...\n");
            run_osc_listener();
            return 0;
        }

        if (strcmp(argv[1], "--export-graph") == 0) {
            export_dot(db, "graph.dot");
            return 0;
        }

        if (strcmp(argv[1], "--dump-series") == 0 && argc == 4) {
            const char *subject = argv[2];
            const char *predicate = argv[3];
            printf("📊 Dumping time series for subject: %s, predicate: %s\n", subject, predicate);

            cJSON *series = query_time_series(db, subject, predicate);
            if (series) {
                char *json_str = cJSON_Print(series);
                if (json_str) {
                    printf("%s\n", json_str);
                    free(json_str);
                }
                cJSON_Delete(series);
            } else {
                printf("❌ Failed to generate time series data.\n");
            }

            sqlite3_close(db);
            if (block) free(block);
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

    // 🧬 Reset block and emit a dummy test message
    block = new_block();
    LOG_INFO("Block: %s\n", block);

    int len = tosc_writeMessage(buffer, BUFFER_SIZE, "/generate_ipv6", "");
    process_osc_message(buffer, len);

    sqlite3_close(db);
    if (block) free(block);

    return 0;
}

