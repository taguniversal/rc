
#include <stdlib.h>
#include<stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "cJSON.h" // or use cJSON if that's what you're already using
#include "eval.h"
#include "log.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cJSON.h"
#include "eval.h"
#include "log.h"

void write_network_json(Block *blk, const char *filename) {
  if (!blk || !filename) return;

  FILE *fp = fopen(filename, "w");
  if (!fp) {
    LOG_ERROR("❌ Could not open output file: %s", filename);
    return;
  }

  cJSON *nodes = cJSON_CreateArray();
  cJSON *links = cJSON_CreateArray();

  // === Emit nodes for Invocations ===
  for (Invocation *inv = blk->invocations; inv; inv = inv->next) {
    cJSON *node = cJSON_CreateObject();
    cJSON_AddStringToObject(node, "id", inv->name);
    cJSON_AddStringToObject(node, "type", "Invocation");
    cJSON_AddItemToArray(nodes, node);

    for (SourcePlace *src = inv->sources; src; src = src->next) {
      if (src->name) {
        cJSON *edge = cJSON_CreateObject();
        cJSON_AddStringToObject(edge, "source", src->name);
        cJSON_AddStringToObject(edge, "target", inv->name);
        cJSON_AddStringToObject(edge, "type", "input");
        cJSON_AddItemToArray(links, edge);
      }
    }
    
    for (DestinationPlace *dst = inv->destinations; dst; dst = dst->next) {
        if (dst->name) {
          // Add destination node
          cJSON *dst_node = cJSON_CreateObject();
          cJSON_AddStringToObject(dst_node, "id", dst->name);
          cJSON_AddStringToObject(dst_node, "type", "Signal");
          cJSON_AddItemToArray(nodes, dst_node);
      
          // Add edge
          cJSON *edge = cJSON_CreateObject();
          cJSON_AddStringToObject(edge, "source", inv->name);
          cJSON_AddStringToObject(edge, "target", dst->name);
          cJSON_AddStringToObject(edge, "type", "output");
          cJSON_AddItemToArray(links, edge);
        }
      }
      
  }

  // === Emit nodes for Definitions ===
  for (Definition *def = blk->definitions; def; def = def->next) {
    cJSON *node = cJSON_CreateObject();
    cJSON_AddStringToObject(node, "id", def->name);
    cJSON_AddStringToObject(node, "type", "Definition");
    cJSON_AddItemToArray(nodes, node);

    for (Invocation *inv = blk->invocations; inv; inv = inv->next) {
      if (inv->definition && strcmp(inv->definition->name, def->name) == 0) {
        cJSON *edge = cJSON_CreateObject();
        cJSON_AddStringToObject(edge, "source", def->name);
        cJSON_AddStringToObject(edge, "target", inv->name);
        cJSON_AddStringToObject(edge, "type", "uses");
        cJSON_AddItemToArray(links, edge);
      }
    }
  }

  // === Root JSON ===
  cJSON *root = cJSON_CreateObject();
  cJSON_AddItemToObject(root, "nodes", nodes);
  cJSON_AddItemToObject(root, "links", links);

  char *json_str = cJSON_Print(root);
  if (json_str) {
    fputs(json_str, fp);
    free(json_str);
  }

  fclose(fp);
  cJSON_Delete(root);

  LOG_INFO("🧠 Logic graph written to: %s", filename);
}
