#include "gap.h"
void init_pubsub(void) ;
void cleanup_pubsub(void);
void publish_signal(const char *signal_name, const char *value);
void subscribe_loop(void (*on_signal)(const char *signal, const char *value));
void publish_packet(const GAPPacket *packet);
GAPPacket *receive_packet(void);
