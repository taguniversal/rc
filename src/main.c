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


int main(int argc, char *argv[]) {
    const char *inv_dir = NULL;
    const char *out_dir = NULL;
    int compile_mode = 0;

    // Parse command-line arguments
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

    init_pubsub();

    // Create GAP packet
    const char *sexpr = "(Invoke (Target NOT) (Args (X 1)))";
    size_t payload_len = strlen(sexpr);

    GAPPacket *pkt = malloc(sizeof(GAPPacket) + payload_len);
    memset(pkt, 0, sizeof(GAPPacket) + payload_len);

    pkt->version = 1;
    pkt->type = GAP_INVOKE;
    pkt->reserved = 0;
    pkt->psi = mkrand_generate_ipv6();  // Assume this generates a unique psi128_t
    pkt->from = in6addr_loopback;
    pkt->to = in6addr_loopback;
    pkt->payload_len = payload_len;
    memcpy(pkt->payload, sexpr, payload_len);

    // Send via pubsub
    printf("📤 Publishing GAP packet with PSI: ");
    print_psi(&pkt->psi);
    printf("\n");
    publish_packet(pkt);

    sleep(1);

    // Try receiving once
    GAPPacket *received = receive_packet();
    if (received) {
        printf("\n🔁 Received GAP packet:\n");
        printf("PSI       : ");
        print_psi(&received->psi);
        printf("\nPayload   : %.*s\n", received->payload_len, received->payload);
        free(received);
    } else {
        printf("\n⚠️ No packet received.\n");
    }

    free(pkt);
    cleanup_pubsub();

    // If --compile mode, run compiler
    if (compile_mode) {
        Block blk = {0};
        blk.psi = mkrand_generate_ipv6();
        compile_block(&blk, inv_dir, out_dir);
    }

    return 0;
}
