#include "cJSON.h" // or use cJSON if that's what you're already using
#include "cJSON.h"
#include "eval.h"
#include "log.h"
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "log.h"

cJSON *find_node_by_id(cJSON *nodes, const char *id)
{
    cJSON *node = NULL;
    cJSON_ArrayForEach(node, nodes)
    {
        cJSON *node_id = cJSON_GetObjectItem(node, "id");
        if (node_id && strcmp(node_id->valuestring, id) == 0)
        {
            return node;
        }
    }
    return NULL;
}
void write_network_json(Block *blk, const char *filename)
{
    if (!blk || !filename)
        return;

    FILE *fp = fopen(filename, "w");
    if (!fp)
    {
        LOG_ERROR("❌ Could not open output file: %s", filename);
        return;
    }

    cJSON *nodes = cJSON_CreateArray();
    cJSON *links = cJSON_CreateArray();

    // === Emit nodes for Definitions ===
    for (Definition *def = blk->definitions; def; def = def->next)
    {
        LOG_INFO("🧠 Emitting definition: %s", def->name);
        cJSON *node = cJSON_CreateObject();
        cJSON_AddStringToObject(node, "id", def->name);
        cJSON_AddStringToObject(node, "type", "Definition");
        cJSON_AddItemToArray(nodes, node);

        for (Invocation *inv = blk->invocations; inv; inv = inv->next)
        {
            if (inv->definition && strcmp(inv->definition->name, def->name) == 0)
            {
                LOG_INFO("📎 Linking definition '%s' to invocation '%s'", def->name, inv->name);
                cJSON *edge = cJSON_CreateObject();
                cJSON_AddStringToObject(edge, "source", def->name);
                cJSON_AddStringToObject(edge, "target", inv->name);
                cJSON_AddStringToObject(edge, "type", "uses");
                cJSON_AddItemToArray(links, edge);
            }
        }
    }

    // === Emit nodes for Invocations and Links ===
    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
        LOG_INFO("🔵 Emitting invocation: %s", inv->name);
        cJSON *node = cJSON_CreateObject();
        cJSON_AddStringToObject(node, "id", inv->name);
        cJSON_AddStringToObject(node, "type", "Invocation");
        cJSON_AddItemToArray(nodes, node);

        // Input links from SourcePlaces
        for (size_t i = 0; i < inv->boundary_sources.count; ++i)
        {
            SourcePlace *src = inv->boundary_sources.items[i];
            if (!src) continue;

            LOG_INFO("🔍 Inspecting SourcePlace for Invocation '%s'", inv->name);
            LOG_INFO("    ➤ src->name: %s", src->name ? src->name : "(null)");
            LOG_INFO("    ➤ src->content: %s", src->content ? src->content : "null");

            if (src->name)
            {
                cJSON *edge = cJSON_CreateObject();
                cJSON_AddStringToObject(edge, "source", src->name);
                cJSON_AddStringToObject(edge, "target", inv->name);
                cJSON_AddStringToObject(edge, "type", "input");
                cJSON_AddItemToArray(links, edge);
            }
            else if (src->content)
            {
                char literal_id[64];
                snprintf(literal_id, sizeof(literal_id), "%s", src->content);

                cJSON *lit_node = cJSON_CreateObject();
                cJSON_AddStringToObject(lit_node, "id", literal_id);
                cJSON_AddStringToObject(lit_node, "type", "Literal");
                cJSON_AddStringToObject(lit_node, "value", src->content);
                cJSON_AddItemToArray(nodes, lit_node);

                cJSON *edge = cJSON_CreateObject();
                cJSON_AddStringToObject(edge, "source", literal_id);
                cJSON_AddStringToObject(edge, "target", inv->name);
                cJSON_AddStringToObject(edge, "type", "input");
                cJSON_AddItemToArray(links, edge);

                LOG_INFO("📦 Added Literal '%s' for Invocation '%s'", src->content, inv->name);
            }
        }

        // Output links from DestinationPlaces
        for (size_t j = 0; j < inv->boundary_destinations.count; ++j)
        {
            DestinationPlace *dst = inv->boundary_destinations.items[j];
            if (!dst || !dst->name) {
                LOG_WARN("⚠️ DestinationPlace with NULL name found.");
                continue;
            }

            LOG_INFO("🧩 Emitting destination: %s", dst->name);

            cJSON *dst_node = find_node_by_id(nodes, dst->name);
            if (!dst_node)
            {
                dst_node = cJSON_CreateObject();
                cJSON_AddStringToObject(dst_node, "id", dst->name);
                cJSON_AddStringToObject(dst_node, "type", "Signal");
                cJSON_AddItemToArray(nodes, dst_node);
            }

            cJSON_ReplaceItemInObject(dst_node, "state", cJSON_CreateString(dst->content ? dst->content : "unknown"));

            cJSON *edge = cJSON_CreateObject();
            cJSON_AddStringToObject(edge, "source", inv->name);
            cJSON_AddStringToObject(edge, "target", dst->name);
            cJSON_AddStringToObject(edge, "type", "output");
            cJSON_AddItemToArray(links, edge);
        }
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "nodes", nodes);
    cJSON_AddItemToObject(root, "links", links);

    char *json_str = cJSON_Print(root);
    if (json_str)
    {
        fputs(json_str, fp);
        free(json_str);
    }

    fclose(fp);
    cJSON_Delete(root);

    LOG_INFO("🧠 Logic graph written to: %s", filename);
}
