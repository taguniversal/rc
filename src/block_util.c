
#include "eval.h"
#include "block.h"
#include <stdio.h>
#include <string.h>

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <arpa/inet.h>  // for in6_addr

bool parse_psi128(const char *input, psi128_t *out)
{
    if (!input || !out) return false;

    // Validate format: starts with "[<:" and ends with ":>]"
    size_t len = strlen(input);
    if (len < 7 || strncmp(input, "[<:", 3) != 0 || strncmp(input + len - 3, ":>]", 3) != 0) {
        return false;
    }

    // Extract hex part
    char hex[33] = {0};  // 32 hex chars + null terminator
    strncpy(hex, input + 3, 32);

    // Parse hex into 16 bytes
    for (int i = 0; i < 16; ++i) {
        char byte_str[3] = { hex[i*2], hex[i*2 + 1], 0 };
        unsigned int byte;
        if (sscanf(byte_str, "%02x", &byte) != 1) {
            return false;
        }
        out->s6_addr[i] = (uint8_t)byte;
    }

    return true;
}

void print_psi(const psi128_t *psi) {
    printf("[<:");
    for (int i = 0; i < 16; i++) {
        printf("%02X", psi->s6_addr[i]);
    }
    printf(":>]");
}

char *psi_to_string(const psi128_t *psi)
{
    if (!psi) return strdup("(null)");

    char *buf = malloc(40); // 32 hex digits + 6 for brackets/colon/terminator
    if (!buf) return strdup("(oom)");

    char *ptr = buf;
    ptr += sprintf(ptr, "[<:");
    for (int i = 0; i < 16; i++)
        ptr += sprintf(ptr, "%02X", psi->s6_addr[i]);
    sprintf(ptr, ":>]");

    return buf;
}
