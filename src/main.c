

#include "block.h"
#include "mkrand.h"
#include "pubsub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main() {
    init_pubsub();

    // Create GAP packet
    const char *sexpr = "(Invoke (Target NOT) (Args (X 1)))";
    size_t payload_len = strlen(sexpr);

    GAPPacket *pkt = malloc(sizeof(GAPPacket) + payload_len);
    memset(pkt, 0, sizeof(GAPPacket) + payload_len);

    pkt->version = 1;
    pkt->type = GAP_INVOKE;
    pkt->reserved = 0;
    pkt->psi = new_block();  // Assume this generates a unique psi128_t
    pkt->from = in6addr_loopback;
    pkt->to = in6addr_loopback;
    pkt->payload_len = payload_len;
    memcpy(pkt->payload, sexpr, payload_len);

    // Send via pubsub
    printf("📤 Publishing GAP packet with PSI: ");
    print_psi(&pkt->psi);
    publish_packet(pkt);

    // Simulate a short delay
    sleep(1);

    // Try receiving (just once for demo)
    GAPPacket *received = receive_packet();
    if (received) {
        printf("\n🔁 Received GAP packet:\n");
        printf("PSI       : ");
        print_psi(&received->psi);
        printf("Payload   : %.*s\n", received->payload_len, received->payload);
        free(received);
    } else {
        printf("\n⚠️ No packet received.\n");
    }

    free(pkt);
    cleanup_pubsub();
    return 0;
}
