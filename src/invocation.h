#ifndef INVOCATION_H
#define INVOCATION_H

#include "sqlite3.h"
#include "eval.h"
void parse_block_from_xml(Block* blk, const char* inv_dir) ;
DefinitionLibrary *create_definition_library() ;
void add_blueprint(DefinitionLibrary *lib, const char *name, const char *filepath);
Definition *parse_definition_from_file(const char *filepath);
void free_definition_library(DefinitionLibrary *lib) ;
#endif