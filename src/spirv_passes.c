#include "spirv.h"
#include "log.h"
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdlib.h>


// Define which ops are considered "preamble" ops
static const char *PRELUDE_OPS[] = {
    "OpCapability",
    "OpMemoryModel",
    "OpTypeInt",
    "OpTypeVoid",
    "OpTypeFunction",
    "OpConstant",
    NULL
};

static bool is_prelude_op(const char *opcode) {
    for (int i = 0; PRELUDE_OPS[i]; ++i) {
        if (strcmp(opcode, PRELUDE_OPS[i]) == 0)
            return true;
    }
    return false;
}

static bool op_equals(SPIRVOp *a, SPIRVOp *b) {
    if (!a || !b) return false;
    if (strcmp(a->opcode, b->opcode) != 0) return false;
    if (a->operand_count != b->operand_count) return false;
    for (size_t i = 0; i < a->operand_count; ++i) {
        if (strcmp(a->operands[i], b->operands[i]) != 0)
            return false;
    }
    return true;
}

void dedupe_prelude_ops(SPIRVModule *mod) {
    if (!mod || !mod->head) return;

    SPIRVOp *seen[128] = {0};
    size_t seen_count = 0;

    SPIRVOp *prev = NULL;
    SPIRVOp *curr = mod->head;

    while (curr) {
        bool is_dup = false;

        if (is_prelude_op(curr->opcode)) {
            for (size_t i = 0; i < seen_count; ++i) {
                if (op_equals(curr, seen[i])) {
                    is_dup = true;
                    break;
                }
            }

            if (!is_dup && seen_count < 128) {
                seen[seen_count++] = curr;
            }
        }

        if (is_dup) {
            LOG_INFO("⚠️ Removing duplicate prelude op: (%s ...)", curr->opcode);

            if (prev) {
                prev->next = curr->next;
            } else {
                mod->head = curr->next;
            }

            if (mod->tail == curr) {
                mod->tail = prev;
            }

            SPIRVOp *tmp = curr;
            curr = curr->next;
            free(tmp->opcode);
            for (size_t i = 0; i < tmp->operand_count; ++i)
                free(tmp->operands[i]);
            free(tmp->operands);
            free(tmp);
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
}
