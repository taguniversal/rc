#include <zmq.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gap.h"  // Assumes GAPPacket and related defs are here

// Global context and socket for reuse (optional)
static void *context = NULL;
static void *pub_socket = NULL;

void init_pubsub()
{
    if (!context)
        context = zmq_ctx_new();

    if (!pub_socket)
    {
        pub_socket = zmq_socket(context, ZMQ_PUB);
        int rc = zmq_bind(pub_socket, "tcp://*:5556");  // Bind to all interfaces
        if (rc != 0)
        {
            perror("zmq_bind failed");
            exit(EXIT_FAILURE);
        }
        printf("📡 ZeroMQ PUB socket bound to tcp://*:5556\n");
    }
}

void publish_packet(const GAPPacket *pkt)
{
    if (!context || !pub_socket)
        init_pubsub();

    size_t total_size = sizeof(GAPPacket) + pkt->payload_len;

    int rc = zmq_send(pub_socket, pkt, total_size, 0);
    if (rc == -1)
    {
        perror("❌ zmq_send failed");
        return;
    }

    printf("✅ Packet published (%lu bytes)\n", total_size);
}

void *subscriber_socket = NULL;

void init_subscriber(const char *endpoint) {
    static void *context = NULL;
    if (!context) context = zmq_ctx_new();

    subscriber_socket = zmq_socket(context, ZMQ_SUB);
    zmq_connect(subscriber_socket, endpoint);
    zmq_setsockopt(subscriber_socket, ZMQ_SUBSCRIBE, "", 0);  // Subscribe to all
}

GAPPacket *receive_packet() {
    if (!subscriber_socket) {
        fprintf(stderr, "❌ Subscriber socket not initialized\n");
        return NULL;
    }

    // Step 1: Receive the raw header (fixed size part)
    GAPPacket header;
    int rc = zmq_recv(subscriber_socket, &header, sizeof(GAPPacket), ZMQ_RCVMORE);
    if (rc == -1) {
        perror("zmq_recv (header)");
        return NULL;
    }

    // Step 2: Allocate full packet including payload
    GAPPacket *pkt = malloc(sizeof(GAPPacket) + header.payload_len);
    if (!pkt) {
        fprintf(stderr, "❌ Out of memory\n");
        return NULL;
    }

    memcpy(pkt, &header, sizeof(GAPPacket));

    // Step 3: Receive the payload (if any)
    if (header.payload_len > 0) {
        rc = zmq_recv(subscriber_socket, pkt->payload, header.payload_len, 0);
        if (rc == -1) {
            perror("zmq_recv (payload)");
            free(pkt);
            return NULL;
        }
    }

    return pkt;
}

