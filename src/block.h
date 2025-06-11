#ifndef BLOCK_H
#define BLOCK_H
#include "string_list.h"
#include "invocation.h"
#include "instance.h"
#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


// === Core Structures ===

typedef struct Block {
    psi128_t psi;
    Invocation *invocations;
    Definition *definitions;
    InstanceList *instances;
} Block;


#endif
