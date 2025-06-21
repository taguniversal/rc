#ifndef SIGNAL_H
#define SIGNAL_H
#include "invocation.h"
#include "signal_map.h"
#include "string_list.h"
#include <stdlib.h>
#include<stdbool.h>


bool all_signals_ready(StringList *input_names, SignalMap *signal_map);



#endif
