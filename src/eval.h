#include "sqlite3.h"

int cycle(sqlite3* db, const char* block, const char* expr_id) ;
void eval(sqlite3* db, const char* block) ;