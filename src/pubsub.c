// pubsub.c
#include "block.h"
#include <czmq.h>
#include <stdio.h>
#include <string.h>

static zsock_t *publisher = NULL;
static zsock_t *subscriber = NULL;

// Call once from main or rcnode init
void init_pubsub(void) {
    publisher = zsock_new_pub("inproc://signals");
    subscriber = zsock_new_sub("inproc://signals", "", 0);  // subscribe to all
    if (!publisher || !subscriber) {
        fprintf(stderr, "❌ Failed to initialize ZeroMQ sockets.\n");
    } else {
        printf("✅ PubSub initialized.\n");
    }
}

void publish_packet(const GAPPacket *packet) {
    if (!publisher) return;
    zframe_t *frame = zframe_new(packet, sizeof(GAPPacket) + packet->payload_len);
    zframe_send(&frame, publisher, 0);
}

void subscribe_loop(void (*on_packet)(const GAPPacket *packet)) {
    if (!subscriber || !on_packet) return;

    while (true) {
        zframe_t *frame = zframe_recv(subscriber);
        if (!frame) continue;

        const GAPPacket *packet = (const GAPPacket *) zframe_data(frame);
        on_packet(packet);

        zframe_destroy(&frame);
    }
}


// Call once at shutdown
void shutdown_pubsub(void) {
    if (subscriber) zsock_destroy(&subscriber);
    if (publisher) zsock_destroy(&publisher);
}
