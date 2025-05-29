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

        for (size_t i = 0; i < inv->boundary_sources.count; ++i)
        {
            SourcePlace *src = inv->boundary_sources.items[i];
            const char *val = (src && src->content) ? src->content : "(null)";
            LOG_INFO("    ➤ Source: %-20s → Content: %s", 
                     (src && src->resolved_name) ? src->resolved_name : "(unnamed)", val);
        }

        for (size_t j = 0; j < inv->boundary_destinations.count; ++j)
        {
            DestinationPlace *dst = inv->boundary_destinations.items[j];
            const char *val = (dst && dst->content) ? dst->content : "(null)";
            LOG_INFO("    ➤ Dest:   %-20s → Content: %s", 
                     (dst && dst->resolved_name) ? dst->resolved_name : "(unnamed)", val);
        }
    }
}
