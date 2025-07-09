#include "rewrite_util.h"
#include "log.h" // your logging macros
#include "log.h"
#include "eval_util.h"
#include "string_list.h"
#include "signal_map.h"
#include <stdio.h>  // for asprintf
#include <stdlib.h> // for free
#include <string.h> // for strcmp
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

NameCounter *counters = NULL;

int get_next_instance_id(const char *name)
{
    for (NameCounter *nc = counters; nc; nc = nc->next)
    {
        if (strcmp(nc->name, name) == 0)
        {
            return ++nc->count;
        }
    }
    NameCounter *nc = calloc(1, sizeof(NameCounter));
    nc->name = strdup(name);
    nc->count = 0;
    nc->next = counters;
    counters = nc;
    return 0;
}


void cleanup_name_counters(void)
{
    while (counters)
    {
        NameCounter *next = counters->next;
        free((void *)counters->name);
        free(counters);
        counters = next;
    }
}

void qualify_literal_bindings(Invocation *inv, const char *instance_name)
{
    if (!inv || !instance_name || !inv->literal_bindings || inv->literal_bindings->count == 0)
    {
        LOG_INFO("ℹ️ No literal bindings to qualify for instance '%s'", instance_name ? instance_name : "(null)");
        return;
    }

    for (size_t i = 0; i < inv->literal_bindings->count; ++i)
    {
        LiteralBinding *b = &inv->literal_bindings->items[i];
        if (!b || !b->name)
            continue;

        if (strchr(b->name, '.'))
        {
            LOG_INFO("✅ Literal binding already qualified: %s", b->name);
            continue;
        }

        char buf[256];
        snprintf(buf, sizeof(buf), "%s.%s", instance_name, b->name);

        free(b->name);
        b->name = strdup(buf);
        LOG_INFO("🔁 Literal binding qualified: %s → %s", b->name, buf);
    }
}

