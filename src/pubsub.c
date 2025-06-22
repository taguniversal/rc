
#include "block.h"
#include "gap.h"
#include "mkrand.h"
#include "signal_map.h"

#include <czmq.h>
#include <stdio.h>
#include <string.h>
#ifdef LOG_INFO
#undef LOG_INFO
#endif
#include "log.h" // Keep AFTER czmq because they define LOG_INFO

static zsock_t *publisher = NULL;
static zsock_t *subscriber = NULL;

/// Initialize PUB/SUB sockets using CZMQ.

void init_pubsub(void)
{
    if (!publisher)
        publisher = zsock_new_pub("inproc://signals");

    if (!subscriber)
        subscriber = zsock_new_sub("inproc://signals", ""); // Subscribe to all topics

    zsock_set_rcvtimeo(subscriber, 100); // ✅ Use CZMQ API, not zmq_setsockopt

    if (!publisher || !subscriber)
    {
        fprintf(stderr, "❌ Failed to initialize CZMQ PUB/SUB sockets.\n");
    }
    else
    {
        printf("✅ PubSub initialized.\n");
    }
}

/// Clean up sockets (use at shutdown)
void cleanup_pubsub(void)
{
    if (publisher)
    {
        zsock_destroy(&publisher);
        publisher = NULL;
    }
    if (subscriber)
    {
        zsock_destroy(&subscriber);
        subscriber = NULL;
    }
    LOG_INFO("🧹 PubSub cleaned up.");
}

/// Publish a packet over the PUB socket
void publish_packet(const GAPPacket *packet)
{
    if (!publisher)
        init_pubsub();

    size_t total_size = sizeof(GAPPacket) + packet->payload_len;
    zframe_t *frame = zframe_new(packet, total_size);
    if (!frame)
    {
        fprintf(stderr, "❌ Failed to allocate zframe.\n");
        return;
    }

    int rc = zframe_send(&frame, publisher, 0);
    if (rc != 0)
    {
        fprintf(stderr, "❌ Failed to send frame\n");
        return;
    }
    zsock_flush(publisher);
    LOG_INFO("📤 Published %lu bytes", total_size);
}

GAPPacket *receive_packet(void)
{
    if (!subscriber)
    {
        LOG_ERROR("❌ Subscriber socket not initialized");
        return NULL;
    }

    zframe_t *frame = zframe_recv(subscriber);
    if (!frame)
    {
        LOG_WARN("⚠️  No frame received");
        return NULL;
    }

    size_t size = zframe_size(frame);
    if (size < sizeof(GAPPacket))
    {
        LOG_ERROR("❌ Frame too small to contain header (%lu bytes)", size);
        zframe_destroy(&frame);
        return NULL;
    }

    const uint8_t *data = zframe_data(frame);
    const GAPPacket *hdr = (const GAPPacket *)data;

    size_t expected_size = sizeof(GAPPacket) + hdr->payload_len;
    if (size < expected_size)
    {
        LOG_ERROR("❌ Incomplete packet: expected %lu bytes, got %lu\n", expected_size, size);
        zframe_destroy(&frame);
        return NULL;
    }

    GAPPacket *pkt = malloc(expected_size);
    if (!pkt)
    {
        LOG_ERROR("❌ Out of memory");
        zframe_destroy(&frame);
        return NULL;
    }

    memcpy(pkt, data, expected_size);
    zframe_destroy(&frame);
    return pkt;
}

void poll_pubsub(SignalMap *signal_map)
{
    int received = 0;

    while (1)
    {
        GAPPacket *packet = receive_packet();
        if (!packet)
        {
            LOG_INFO("Polled, no traffic");
            break;
        }
        const char *payload = (const char *)packet->payload;
        if (!payload || packet->payload_len == 0)
        {
            LOG_WARN("⚠️ PubSub packet has empty payload");
            free(packet);
            continue;
        }

        // Assuming payload is formatted as: "SIGNAL_NAME=VALUE"
        const char *sep = strchr(payload, '=');
        if (!sep)
        {
            LOG_WARN("⚠️ Malformed pubsub payload: %s", payload);
            free(packet);
            continue;
        }

        size_t name_len = sep - payload;
        char signal_name[256];
        char signal_value[256];

        snprintf(signal_name, sizeof(signal_name), "%.*s", (int)name_len, payload);
        snprintf(signal_value, sizeof(signal_value), "%s", sep + 1);

        update_signal_value(signal_map, signal_name, signal_value);
        LOG_INFO("📬 PubSub delivered: %s = %s", signal_name, signal_value);

        free(packet);
        received++;
    }

    if (received > 0)
    {
        LOG_INFO("📡 PubSub polling complete — %d signal(s) injected", received);
    }
}

/// Loop receiving packets on the SUB socket and invoking a callback
void subscribe_loop(void (*on_packet)(const GAPPacket *packet))
{
    if (!subscriber || !on_packet)
        return;

    while (true)
    {
        zframe_t *frame = zframe_recv(subscriber);
        if (!frame)
            continue;

        const GAPPacket *packet = (const GAPPacket *)zframe_data(frame);
        on_packet(packet);

        zframe_destroy(&frame);
    }
}

void publish_signal(const char *signal_name, const char *value)
{
    if (!publisher)
        init_pubsub();

    // Construct payload as "signal_name=value"
    char payload[512]; // Arbitrary upper bound; increase if needed
    int written = snprintf(payload, sizeof(payload), "%s=%s", signal_name, value);

    if (written < 0 || written >= sizeof(payload))
    {
        fprintf(stderr, "❌ Payload too long or formatting error\n");
        return;
    }

    size_t payload_len = (size_t)written + 1; // Include null terminator

    GAPPacket *pkt = malloc(sizeof(GAPPacket) + payload_len);
    if (!pkt)
    {
        fprintf(stderr, "❌ Failed to allocate GAPPacket\n");
        return;
    }

    memset(pkt, 0, sizeof(GAPPacket) + payload_len);
    pkt->version = 1;
    pkt->type = GAP_SIGNAL;
    pkt->from = in6addr_loopback;
    pkt->to = in6addr_loopback;
    pkt->payload_len = payload_len;

    memcpy(pkt->payload, payload, payload_len);

    LOG_INFO("📡 Publishing signal: %s = %s", signal_name, value);

    publish_packet(pkt);
    free(pkt);
}

