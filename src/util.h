char *prefix_name(const char *def_name, const char *signal_name);   
char *extract_left_of_colon(const char *str);
void db_state(sqlite3 *db, const char *block);
int validate_json_structure(cJSON *root);