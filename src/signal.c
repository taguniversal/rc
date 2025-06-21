
#include<stdbool.h>
#include "string_list.h"
#include "signal_map.h"
#include "signal.h"

bool all_signals_ready(StringList *input_names, SignalMap *signal_map) {
    if (!input_names || string_list_count(input_names) == 0) {
        return false;
    }

    size_t count = string_list_count(input_names);
    for (size_t i = 0; i < count; ++i) {
        const char *name = string_list_get_by_index(input_names, i);
        const char *value = get_signal_value(signal_map, name);

        if (!value) {
            return false; // 🛑 Input not ready
        }
    }

    return true; // ✅ All inputs have values
}


