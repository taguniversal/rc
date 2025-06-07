#include "block.h"
#include "block_util.h"
#include "mkrand.h"
#include "string_list.h"
#include <stdlib.h>
#include <string.h>



void print_block(const Block *blk) {
    printf("Block psi: ");
    print_psi(blk->psi);
    printf("\n");

    for (UnitList *ul = blk->units; ul; ul = ul->next) {
        Unit *u = ul->unit;
        printf("  Unit: %s\n", u->name);
        if (u->definition) {
            printf("    Definition: %s\n", u->definition->name);
            printf("    Inputs: ");
            for (char **p = u->definition->inputs; p && *p; p++) printf("%s ", *p);
            printf("\n    Outputs: ");
            for (char **p = u->definition->outputs; p && *p; p++) printf("%s ", *p);
            printf("\n");
        }
        if (u->invocation) {
            printf("    Invocation Target: %s\n", u->invocation->target_name);
            printf("    To: ");
            print_psi(u->invocation->to);
            printf("\n    Inputs: ");
            for (char **p = u->invocation->inputs; p && *p; p++) printf("%s ", *p);
            printf("\n");
        }
    }
}

