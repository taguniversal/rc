// udp_send.c
#include "tinyosc.h"
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#define OSC_PORT_RCV 4243

void send_osc_float(const char *address, float value) {
    char buf[1024];
    int len = tosc_writeMessage(buf, sizeof(buf), address, "f", value);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(OSC_PORT_RCV); // Port your UI listens on
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    sendto(sock, buf, len, 0, (struct sockaddr *)&server, sizeof(server));
    close(sock);
}
