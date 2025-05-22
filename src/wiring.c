#include "wiring.h"
#include "log.h"
#include "eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eval.h"
#include "log.h"  // or whatever your logging header is called

void dump_wiring(Block *blk)
{
    LOG_INFO("🧪 Dumping wiring for all invocations in block PSI: %s", blk->psi);

    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
        LOG_INFO("  🔽 Invocation: %s", inv->name);

        for (SourcePlace *src = inv->sources; src; src = src->next)
        {
            const char *val = (src->content) ? src->content : "(null)";
            LOG_INFO("    ➤ Source: %-20s → Content: %s", src->resolved_name ? src->resolved_name : "(unnamed)", val);
        }

        for (DestinationPlace *dst = inv->destinations; dst; dst = dst->next)
        {
            const char *val = (dst->content) ? dst->content : "(null)";
            LOG_INFO("    ➤ Dest:   %-20s → Content: %s", dst->resolved_name ? dst->resolved_name : "(unnamed)", val);
        }
    }
}
