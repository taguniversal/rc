#include <zmq.h>

void send_signal(const char *name, const char *payload) {
    zmq_send(pub_socket, name, strlen(name), ZMQ_SNDMORE);
    zmq_send(pub_socket, payload, strlen(payload), 0);
}

void *signal_bus_thread(void *arg) {
    void *sub = zmq_socket(ctx, ZMQ_SUB);
    zmq_connect(sub, "inproc://bus");
    zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0);

    char topic[256];
    char message[1024];
    while (1) {
        zmq_recv(sub, topic, sizeof(topic), 0);
        zmq_recv(sub, message, sizeof(message), 0);
        dispatch_signal(topic, message);
    }
}
