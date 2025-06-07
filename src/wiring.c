#include "wiring.h"
#include "log.h"
#include "eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eval.h"
#include "log.h"  
void dump_wiring(Block *blk)
{
    LOG_INFO("🧪 Dumping wiring for all units in block PSI: %s", blk->psi);

    for (UnitList *ul = blk->units; ul; ul = ul->next)
    {
        Unit *unit = ul->unit;
        if (!unit) continue;

        LOG_INFO("  🔽 Unit: %s", unit->name);

        // Dump Invocation Sources (input to the unit)
        if (unit->invocation)
        {
            for (size_t i = 0; i < unit->invocation->boundary_sources.count; ++i)
            {
                SourcePlace *src = unit->invocation->boundary_sources.items[i];
                const char *val = (src && src->content) ? src->content : "(null)";
                LOG_INFO("    ➤ [Invocation] Source: %-20s → Content: %s", 
                         (src && src->resolved_name) ? src->resolved_name : "(unnamed)", val);
            }

            for (size_t j = 0; j < unit->invocation->boundary_destinations.count; ++j)
            {
                DestinationPlace *dst = unit->invocation->boundary_destinations.items[j];
                const char *val = (dst && dst->content) ? dst->content : "(null)";
                LOG_INFO("    ➤ [Invocation] Dest:   %-20s → Content: %s", 
                         (dst && dst->resolved_name) ? dst->resolved_name : "(unnamed)", val);
            }
        }

        // Dump Definition Internal Signals (inside the unit's logic)
        if (unit->definition)
        {
            for (size_t i = 0; i < unit->definition->boundary_sources.count; ++i)
            {
                SourcePlace *src = unit->definition->boundary_sources.items[i];
                const char *val = (src && src->content) ? src->content : "(null)";
                LOG_INFO("    ➤ [Definition] Source: %-20s → Content: %s", 
                         (src && src->resolved_name) ? src->resolved_name : "(unnamed)", val);
            }

            for (size_t j = 0; j < unit->definition->boundary_destinations.count; ++j)
            {
                DestinationPlace *dst = unit->definition->boundary_destinations.items[j];
                const char *val = (dst && dst->content) ? dst->content : "(null)";
                LOG_INFO("    ➤ [Definition] Dest:   %-20s → Content: %s", 
                         (dst && dst->resolved_name) ? dst->resolved_name : "(unnamed)", val);
            }
        }
    }
}

