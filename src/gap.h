#ifndef GAP_H
#define GAP_H

#include "invocation.h"
#include <stdlib.h>

typedef enum {
    GAP_INVOKE    = 0,  // Used for sending an evaluation request
    GAP_RESPONSE  = 1,  // Used to reply to an invocation
    GAP_SIGNAL    = 2,  // Direct signal propagation (e.g., OUT<1>
} GAPPacketType;

typedef struct {
    uint8_t version;
    GAPPacketType type;              // INVOKE = 0, RESPONSE = 1
    uint16_t reserved;
    psi128_t psi;             
    struct in6_addr from;
    struct in6_addr to;
    uint32_t payload_len;
    uint8_t payload[];         // Encoded S-expression or binary
} __attribute__((aligned)) GAPPacket;

typedef struct {
    struct in6_addr addr;
    psi128_t psi;
    uint32_t capabilities;
    char *label;
} GAPNodeDescriptor;


#endif
