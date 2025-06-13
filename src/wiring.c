#include "wiring.h"
#include "log.h"
#include "eval.h"
#include "instance.h"
#include "eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void dump_wiring(Block *blk)
{
    LOG_INFO("🧪 Dumping wiring for all instances in block PSI: %s", blk->psi);

    for (InstanceList *ul = blk->instances; ul; ul = ul->next)
    {
        Instance *instance = ul->instance;
        if (!instance)
            continue;

        LOG_INFO("  🔽 Instance: %s", instance->name);

        // Invocation details
        if (instance->invocation)
        {
            LOG_INFO("    ▶ Invocation Target: %s", instance->invocation->target_name);

            LOG_INFO("    ▶ Input Signals:");
            for (size_t i = 0; i < string_list_count(instance->invocation->input_signals); ++i)
            {
                LOG_INFO("       - %s", string_list_get_by_index(instance->invocation->input_signals, i));
            }

            LOG_INFO("    ▶ Output Signals:");
            for (size_t i = 0; i < string_list_count(instance->invocation->output_signals); ++i)
            {
                LOG_INFO("       - %s", string_list_get_by_index(instance->invocation->output_signals, i));
            }

            if (instance->invocation->literal_bindings && instance->invocation->literal_bindings->count > 0)
            {
                LOG_INFO("    ▶ Literal Bindings:");
                for (size_t i = 0; i < instance->invocation->literal_bindings->count; ++i)
                {
                    LiteralBinding *binding = &instance->invocation->literal_bindings->items[i];
                    LOG_INFO("       - %s = %s", binding->name, binding->value);
                }
            }
        }

        // Definition details
        if (instance->definition)
        {
            LOG_INFO("    ▶ Definition: %s", instance->definition->name);

            LOG_INFO("    ▶ Definition Inputs:");
            for (size_t i = 0; i < string_list_count(instance->definition->input_signals); ++i)
            {
                LOG_INFO("       - %s", string_list_get_by_index(instance->definition->input_signals, i));
            }

            LOG_INFO("    ▶ Definition Outputs:");
            if (instance->definition->output_signals)
            {
                StringList *sl = instance->definition->output_signals;
                for (size_t i = 0; i < string_list_count(sl); ++i)
                {
                    LOG_INFO("       - %s", string_list_get_by_index(sl, i));
                }
            }

            if (instance->definition->conditional_invocation)
            {
                LOG_INFO("    ▶ Conditional Logic:");
                ConditionalInvocation *ci = instance->definition->conditional_invocation;

                LOG_INFO("       Pattern Args:");
                for (size_t i = 0; i < ci->arg_count; ++i)
                {
                    LOG_INFO("         - %s", ci->pattern_args[i]);
                }

                LOG_INFO("       Output: %s", ci->output);

                for (size_t i = 0; i < ci->case_count; ++i)
                {
                    LOG_INFO("       Case: %s → %s", ci->cases[i].pattern, ci->cases[i].result);
                }
            }
        }
    }
}
