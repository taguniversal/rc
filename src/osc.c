
#include "eval.h"
#include "log.h"
#include "mkrand.h"
#include "rdf.h"
#include "sqlite3.h"
#include "tinyosc.h"
#include "util.h"
#include "wiring.h"
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define OSC_BUFFER_SIZE 1024

char *enrich(const char *osc_path) {
  // Strip leading '/' if present
  const char *base_name = (osc_path[0] == '/') ? osc_path + 1 : osc_path;

  // Construct file path (e.g., "ui/radar1.json")
  char filename[128];
  snprintf(filename, sizeof(filename), "ui/%s.json", base_name);

  FILE *file = fopen(filename, "rb");
  if (!file) {
    fprintf(stderr, "❌ enrich(): Could not open %s\n", filename);
    return strdup(""); // Return empty string if not found
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long len = ftell(file);
  rewind(file);

  // Allocate buffer and read
  char *content = malloc(len + 1);
  fread(content, 1, len, file);
  content[len] = '\0';
  fclose(file);

  return content;
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
  fflush(stdout); // <---- THIS
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

  sendto(sockfd, msg, len, 0, (struct sockaddr *)&touchosc_addr,
         sizeof(touchosc_addr));
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

const char *lookup_name_by_osc_path(sqlite3 *db, const char *osc_path) {
  static char name[128];
  sqlite3_stmt *stmt;

  // Step 1: Find subject where inv:from = osc_path
  const char *sql = "SELECT subject FROM triples WHERE predicate = 'inv:from' "
                    "AND object = ? LIMIT 1;";
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return NULL;

  sqlite3_bind_text(stmt, 1, osc_path, -1, SQLITE_STATIC);

  char subject[128];
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char *subj = sqlite3_column_text(stmt, 0);
    snprintf(subject, sizeof(subject), "%s", subj);
    sqlite3_finalize(stmt);
  } else {
    sqlite3_finalize(stmt);
    return NULL;
  }

  // Step 2: Look up inv:name for that subject
  const char *sql2 = "SELECT object FROM triples WHERE subject = ? AND "
                     "predicate = 'inv:name' LIMIT 1;";
  if (sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL) != SQLITE_OK)
    return NULL;

  sqlite3_bind_text(stmt, 1, subject, -1, SQLITE_STATIC);

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char *val = sqlite3_column_text(stmt, 0);
    snprintf(name, sizeof(name), "%s", val);
    sqlite3_finalize(stmt);
    return name;
  }

  sqlite3_finalize(stmt);
  return NULL;
}

// Template pattern for OSC messages with writes:
#define STORE_TRIPLE(subject, pred, value)                                     \
  do {                                                                         \
    insert_triple(db, block, subject, pred, value);                            \
    side_effect = 1;                                                           \
  } while (0)

int process_osc_message(sqlite3 *db, const char *buffer, const char *block,
                        int len) {
  int side_effect = 0;

  tosc_message osc;
  LOG_INFO("📡 Raw OSC Message: %s\n", buffer);

  // Memory usage debug
  sqlite3_int64 mem_used = sqlite3_memory_used();
  sqlite3_int64 mem_high = sqlite3_memory_highwater(1);
  LOG_INFO("SQLite Memory Used: %lld bytes, Highwater: %lld bytes\n", mem_used,
           mem_high);

  // ISO timestamp
  char iso_timestamp[32];
  time_t now = time(NULL);
  struct tm *tm_info = gmtime(&now);
  strftime(iso_timestamp, sizeof(iso_timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
  LOG_INFO("⏱️  Timestamp: %s\n", iso_timestamp);

  if (tosc_parseMessage(&osc, (char *)buffer, len) == 0) {
    LOG_INFO("📡 Parsed OSC Address: %s\n", osc.buffer);
    LOG_INFO("📡 Extracted OSC Format: %s\n",
             osc.format ? osc.format : "(none)");

    if (strcmp(osc.buffer, "/init") == 0) {
      float pressed =
          (osc.format && osc.format[0] == 'f') ? tosc_getNextFloat(&osc) : 0.0f;
      if (pressed == 1.0f) {

        LOG_INFO("⚙️  OSC Init received — seeding row_driver:CA:X values...\n");
        LOG_INFO("🧬 Using block ID: [%s]", block);

        for (int i = 0; i < 128; i++) {
          char subject[64], content[8];
          snprintf(subject, sizeof(subject), "CA:%d", i);

          int bit = (i == 64) ? 1 : 0; // center cell gets the fire
          snprintf(content, sizeof(content), "%d", bit);

          STORE_TRIPLE(subject, "rdf:type", "inv:SourcePlace");
          STORE_TRIPLE(subject, "inv:name", subject);
          STORE_TRIPLE(subject, "inv:hasContent", content);
        }

        LOG_INFO(
            "✅ Init complete. Center cell lit. Pattern awaits evolution.\n");
        db_state(db, block);
        dump_wiring(db, block);
      } else {
        LOG_INFO("🛑 OSC Init ignored (pressed = %.1f)\n", pressed);
      }
    }

    if (strcmp(osc.buffer, "/generate_ipv6") == 0) {
      char ipv6_str[INET6_ADDRSTRLEN];
      uint8_t pubkey[32] = {0x01, 0x02, 0x03};
      generate_ipv6_address(pubkey, ipv6_str);
      LOG_INFO("✅ Generated IPv6: %s\n", ipv6_str);
      return 0; // No RDF write
    }

    if (strcmp(osc.buffer, "/poll") == 0) {
      LOG_INFO("📡 Received /poll\n");
      send_osc_response_str("/poll_response", "{\"status\":\"ok\"}");
      return 0;
    }

    if (strcmp(osc.buffer, "/button1") == 0) {
      float val = (osc.format[0] == 'f') ? tosc_getNextFloat(&osc) : 0.0f;
      LOG_INFO("🟢 Button 1 sent value: %f\n", val);

      char val_str[32];
      snprintf(val_str, sizeof(val_str), "%f", val);

      STORE_TRIPLE("/button1", "context", enrich("/button1"));
      STORE_TRIPLE("/button1", "pressedValue", val_str);
      STORE_TRIPLE("/button1", "timestamp", iso_timestamp);

      send_osc_response("/button1_response", val + 1.0);
    }

    if (strncmp(osc.buffer, "/grid1/", 7) == 0) {
      int row = atoi(osc.buffer + 7);
      float value = tosc_getNextFloat(&osc);
      char val_str[32], subject[32];
      snprintf(val_str, sizeof(val_str), "%f", value);
      snprintf(subject, sizeof(subject), "/grid1/%d", row);

      STORE_TRIPLE("/grid1", "context", enrich("/grid1"));
      STORE_TRIPLE(subject, "rowValue", val_str);
      STORE_TRIPLE(subject, "timestamp", iso_timestamp);
    }

    if (strcmp(osc.buffer, "/radar1") == 0) {
      float x = tosc_getNextFloat(&osc);
      float y = tosc_getNextFloat(&osc);
      char x_str[32], y_str[32];
      snprintf(x_str, sizeof(x_str), "%f", x);
      snprintf(y_str, sizeof(y_str), "%f", y);

      STORE_TRIPLE("/radar1", "context", enrich("/radar1"));
      STORE_TRIPLE("/radar1", "hasX", x_str);
      STORE_TRIPLE("/radar1", "hasY", y_str);
      STORE_TRIPLE("/radar1", "timestamp", iso_timestamp);
    }

    if (strcmp(osc.buffer, "/pager1") == 0) {
      int page = tosc_getNextInt32(&osc);
      char page_str[16];
      snprintf(page_str, sizeof(page_str), "%d", page);

      STORE_TRIPLE("/pager1", "context", enrich("/pager1"));
      STORE_TRIPLE("/pager1", "selectedPage", page_str);
      STORE_TRIPLE("/pager1", "timestamp", iso_timestamp);
    }

    if (strcmp(osc.buffer, "/encoder1") == 0) {
      float value = tosc_getNextFloat(&osc);
      char value_str[32];
      snprintf(value_str, sizeof(value_str), "%f", value);

      STORE_TRIPLE("/encoder1", "context", enrich("/encoder1"));
      STORE_TRIPLE("/encoder1", "rotatedTo", value_str);
      STORE_TRIPLE("/encoder1", "timestamp", iso_timestamp);
    }

    if (strcmp(osc.buffer, "/radio1") == 0) {
      int selection = tosc_getNextInt32(&osc);
      char sel_str[16];
      snprintf(sel_str, sizeof(sel_str), "%d", selection);

      STORE_TRIPLE("/radio1", "context", enrich("/radio1"));
      STORE_TRIPLE("/radio1", "selectedOption", sel_str);
      STORE_TRIPLE("/radio1", "timestamp", iso_timestamp);
    }

    if (strcmp(osc.buffer, "/xy1") == 0) {
      float x = tosc_getNextFloat(&osc);
      float y = tosc_getNextFloat(&osc);
      char x_str[32], y_str[32];
      snprintf(x_str, sizeof(x_str), "%f", x);
      snprintf(y_str, sizeof(y_str), "%f", y);

      STORE_TRIPLE("/xy1", "context", enrich("/xy1"));
      STORE_TRIPLE("/xy1", "positionX", x_str);
      STORE_TRIPLE("/xy1", "positionY", y_str);
      STORE_TRIPLE("/xy1", "timestamp", iso_timestamp);
    }

    if (strcmp(osc.buffer, "/fader5") == 0) {
      float value = tosc_getNextFloat(&osc);
      char value_str[32];
      snprintf(value_str, sizeof(value_str), "%f", value);

      STORE_TRIPLE("/fader5", "context", enrich("/fader5"));
      STORE_TRIPLE("/fader5", "faderValue", value_str);
      STORE_TRIPLE("/fader5", "timestamp", iso_timestamp);
    }
  } else {
    LOG_ERROR("❌ Error parsing OSC message\n");
  }

  return side_effect;
}

void process_osc_response(sqlite3 *db, char *buffer, const char *block,
                          int len) {
  tosc_message osc;
  tosc_parseMessage(&osc, buffer, len);

  const char *addr = tosc_getAddress(&osc);
  const char *name = lookup_name_by_osc_path(db, addr);
  if (!name)
    return;

  LOG_INFO("Found source %s\n", name);

  char dests[10][128]; // Support up to 10 fan-out targets
  int num = lookup_destinations_for_name(db, block, name, dests, 10);
  if (num == 0)
    return;

  float v = tosc_getNextFloat(&osc);

  for (int i = 0; i < num; i++) {
    LOG_INFO("🔁 Fanout %s → %s (via name: %s) with value: %f\n", addr,
             dests[i], name, v);
    send_osc_response(dests[i], v);
  }
}

void run_osc_listener(sqlite3 *db, const char *block,
                      volatile sig_atomic_t *keep_running_ptr) {
  int sockfd;
  struct sockaddr_in servaddr, cliaddr;
  socklen_t len;
  ssize_t n;
  char buffer[OSC_BUFFER_SIZE];

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
  timeout.tv_sec = 1; // check for signals every 1 second
  timeout.tv_usec = 0;
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  LOG_INFO("🎧 Listening for OSC on port %d...\n", OSC_PORT_XMIT);

  while (*keep_running_ptr) {
    len = sizeof(cliaddr);
    n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&cliaddr,
                 &len);
    if (n > 0) {
      process_osc_message(db, buffer, block, n);
      eval(db, block);
      process_osc_response(db, buffer, block, n);
    } else {
      // Timeout occurred — keep ticking the simulation forward
      LOG_INFO("⏳ Timeout — triggering background eval cycle...\n");
      eval(db, block);
    }

  }

  LOG_INFO("👋 Shutting down OSC listener.\n");
  close(sockfd);
}