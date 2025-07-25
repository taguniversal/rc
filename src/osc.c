
#include "eval.h"
#include "log.h"
#include "mkrand.h"
#include "sqlite3.h"
#include "tinyosc.h"
#include "util.h"
#include "wiring.h"
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <signal.h>  // for sig_atomic_t
#include "config.h"

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

// Template pattern for OSC messages with writes:
#define STORE_TRIPLE(subject, pred, value)                                     \
  do {                                                                         \
    insert_triple(db, block, subject, pred, value);                            \
    side_effect = 1;                                                           \
  } while (0)

int process_osc_message(Block* blk, const char *buffer, int len) {
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
        LOG_INFO("🧬 Using block ID: [%s]", blk->psi);

        for (int i = 0; i < 128; i++) {
          char subject[64], content[8];
          snprintf(subject, sizeof(subject), "CA:%d", i);

          int bit = (i == 64) ? 1 : 0; // center cell gets the fire
          snprintf(content, sizeof(content), "%d", bit);
        
        }

        LOG_INFO(
            "✅ Init complete. Center cell lit. Pattern awaits evolution.\n");

      } else {
        LOG_INFO("🛑 OSC Init ignored (pressed = %.1f)\n", pressed);
      }
    }

    if (strcmp(osc.buffer, "/generate_ipv6") == 0) {
      struct in6_addr address_ipv6 = mkrand_generate_ipv6();
      
   //   LOG_INFO("✅ Generated IPv6: %s\n", ipv6_str);
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


      send_osc_response("/button1_response", val + 1.0);
    }

    if (strncmp(osc.buffer, "/grid1/", 7) == 0) {
      int row = atoi(osc.buffer + 7);
      float value = tosc_getNextFloat(&osc);
      char val_str[32], subject[32];
      snprintf(val_str, sizeof(val_str), "%f", value);
      snprintf(subject, sizeof(subject), "/grid1/%d", row);

    }

    if (strcmp(osc.buffer, "/radar1") == 0) {
      float x = tosc_getNextFloat(&osc);
      float y = tosc_getNextFloat(&osc);
      char x_str[32], y_str[32];
      snprintf(x_str, sizeof(x_str), "%f", x);
      snprintf(y_str, sizeof(y_str), "%f", y);


    }

    if (strcmp(osc.buffer, "/pager1") == 0) {
      int page = tosc_getNextInt32(&osc);
      char page_str[16];
      snprintf(page_str, sizeof(page_str), "%d", page);


    }

    if (strcmp(osc.buffer, "/encoder1") == 0) {
      float value = tosc_getNextFloat(&osc);
      char value_str[32];
      snprintf(value_str, sizeof(value_str), "%f", value);

    }

    if (strcmp(osc.buffer, "/radio1") == 0) {
      int selection = tosc_getNextInt32(&osc);
      char sel_str[16];
      snprintf(sel_str, sizeof(sel_str), "%d", selection);

    }

    if (strcmp(osc.buffer, "/xy1") == 0) {
      float x = tosc_getNextFloat(&osc);
      float y = tosc_getNextFloat(&osc);
      char x_str[32], y_str[32];
      snprintf(x_str, sizeof(x_str), "%f", x);
      snprintf(y_str, sizeof(y_str), "%f", y);

    }

    if (strcmp(osc.buffer, "/fader5") == 0) {
      float value = tosc_getNextFloat(&osc);
      char value_str[32];
      snprintf(value_str, sizeof(value_str), "%f", value);

    }
  } else {
    LOG_ERROR("❌ Error parsing OSC message\n");
  }

  return side_effect;
}

void process_osc_response(Block* blk, char *buffer,  int len) {
  tosc_message osc;
  tosc_parseMessage(&osc, buffer, len);

  const char *addr = tosc_getAddress(&osc);

  LOG_INFO("process_osc_response %s Address: %s\n", blk->psi, addr);

}

void run_osc_listener(Block* blk,
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
      process_osc_message(blk, buffer, n);
   //TODO   eval(blk);
      process_osc_response(blk, buffer, n);
    } else {
      // Timeout occurred — keep ticking the simulation forward
      LOG_INFO("⏳ Timeout — triggering background eval cycle...\n");
  //TODO    eval(blk);
    }

  }

  LOG_INFO("👋 Shutting down OSC listener.\n");
  close(sockfd);
}
