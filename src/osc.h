#include "sqlite3.h"
#include "eval.h"

void run_osc_listener(Block* blk, volatile sig_atomic_t* keep_running_ptr);
void process_osc_response(Block* blk, char *buffer, int len);
int process_osc_message(Block* blk, const char *buffer, int len);
void send_osc_response_str(const char *address, const char *str);
void send_osc_response(const char *address, float value);