// pubsub.c
#include "block.h"
#include "gap.h"
#include <czmq.h>
#include <stdio.h>
#include <string.h>

static zsock_t *publisher = NULL;
static zsock_t *subscriber = NULL;

/// Initialize PUB/SUB sockets using CZMQ.
/// Call this once at startup.
void init_pubsub(void) {
    if (!publisher)
        publisher = zsock_new_pub("inproc://signals");

    if (!subscriber)
        subscriber = zsock_new_sub("inproc://signals", "");  // Subscribe to all topics

    if (!publisher || !subscriber) {
        fprintf(stderr, "❌ Failed to initialize CZMQ PUB/SUB sockets.\n");
    } else {
        printf("✅ PubSub initialized.\n");
    }
}

/// Clean up sockets (use at shutdown)
void cleanup_pubsub(void) {
    if (publisher) {
        zsock_destroy(&publisher);
        publisher = NULL;
    }
    if (subscriber) {
        zsock_destroy(&subscriber);
        subscriber = NULL;
    }
    printf("🧹 PubSub cleaned up.\n");
}

/// Publish a packet over the PUB socket
void publish_packet(const GAPPacket *packet) {
    if (!publisher)
        init_pubsub();

    size_t total_size = sizeof(GAPPacket) + packet->payload_len;
    zframe_t *frame = zframe_new(packet, total_size);
    if (!frame) {
        fprintf(stderr, "❌ Failed to allocate zframe.\n");
        return;
    }

    int rc = zframe_send(&frame, publisher, 0);
    if (rc != 0) {
        fprintf(stderr, "❌ Failed to send frame\n");
        return;
    }

    printf("📤 Published %lu bytes\n", total_size);
}

GAPPacket *receive_packet(void) {
    if (!subscriber) {
        fprintf(stderr, "❌ Subscriber socket not initialized\n");
        return NULL;
    }

    zframe_t *frame = zframe_recv(subscriber);
    if (!frame) {
        fprintf(stderr, "⚠️  No frame received\n");
        return NULL;
    }

    size_t size = zframe_size(frame);
    if (size < sizeof(GAPPacket)) {
        fprintf(stderr, "❌ Frame too small to contain header (%lu bytes)\n", size);
        zframe_destroy(&frame);
        return NULL;
    }

    const uint8_t *data = zframe_data(frame);
    const GAPPacket *hdr = (const GAPPacket *) data;

    size_t expected_size = sizeof(GAPPacket) + hdr->payload_len;
    if (size < expected_size) {
        fprintf(stderr, "❌ Incomplete packet: expected %lu bytes, got %lu\n", expected_size, size);
        zframe_destroy(&frame);
        return NULL;
    }

    GAPPacket *pkt = malloc(expected_size);
    if (!pkt) {
        fprintf(stderr, "❌ Out of memory\n");
        zframe_destroy(&frame);
        return NULL;
    }

    memcpy(pkt, data, expected_size);
    zframe_destroy(&frame);
    return pkt;
}


/// Loop receiving packets on the SUB socket and invoking a callback
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
