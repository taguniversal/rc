#include "sqlite3.h"

void run_osc_listener(sqlite3* db, const char* block, volatile sig_atomic_t* keep_running_ptr);
void process_osc_response(sqlite3* db, char *buffer, const char* block, int len);
int process_osc_message(sqlite3* db, const char *buffer, const char* block, int len);
const char* lookup_name_by_osc_path(sqlite3* db, const char* osc_path);
void send_osc_response_str(const char *address, const char *str);
void send_osc_response(const char *address, float value);