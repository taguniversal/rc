void init_pubsub(void) ;
void publish_signal(const char *signal_name, const char *value);
void subscribe_loop(void (*on_signal)(const char *signal, const char *value));
