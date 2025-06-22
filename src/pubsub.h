#include "gap.h"
#include "signal_map.h"
void init_pubsub(void) ;
void cleanup_pubsub(void);
void publish_signal(const char *signal_name, const char *value);
void subscribe_loop(void (*on_signal)(const char *signal, const char *value));
void publish_packet(const GAPPacket *packet);
void poll_pubsub(SignalMap *signal_map);
GAPPacket *receive_packet(void);
