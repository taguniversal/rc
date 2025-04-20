#ifndef WIRING_H
#define WIRING_H

#include <sqlite3.h>

void dump_wiring(sqlite3* db, const char* block);

#endif
