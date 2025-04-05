
#include "rdf.h"
#include "sqlite3.h"
#include "log.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <stdbool.h>
#include <string.h>
#include "cJSON.h"



char* load_file(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    rewind(file);

    char* buffer = malloc(len + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    fread(buffer, 1, len, file);
    buffer[len] = '\0';
    fclose(file);
    return buffer;
}

const char* anon_id(void) {
    return "0";
 }

 const char* get_value(cJSON* root, const char* key) {
    cJSON* val = cJSON_GetObjectItem(root, key);
    if (val && cJSON_IsString(val)) {
        return val->valuestring;
    }
    return NULL;
}\

bool is_type(cJSON* root, const char* type) {
    cJSON* t = cJSON_GetObjectItem(root, "@type");
    if (!t) return false;

    if (cJSON_IsArray(t)) {
        cJSON* item = NULL;
        cJSON_ArrayForEach(item, t) {
            if (cJSON_IsString(item) && strcmp(item->valuestring, type) == 0) {
                return true;
            }
        }
    } else if (cJSON_IsString(t)) {
        return strcmp(t->valuestring, type) == 0;
    }

    return false;
}

void map_io(sqlite3* db, const char* block, const char* inv_dir) {
    DIR* dir = opendir(inv_dir);
    struct dirent* entry;
    bool matched = false;
    LOG_INFO("🔍 map_io starting. in %s\n", inv_dir);

    if (!dir) {
        LOG_ERROR("❌ '%s' directory not found. Skipping map_io.", inv_dir);
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".json")) {
            char path[256];
            snprintf(path, sizeof(path), "inv/%s", entry->d_name);
            LOG_INFO("Loading %s\n", path);
            char* json = load_file(path);
            if (!json) continue;

            cJSON* root = cJSON_Parse(json);
            if (!root) {
                free(json);
                continue;
            }

            if (is_type(root, "inv:SourcePlace")) {
                matched = true;
            
                const char* name    = get_value(root, "inv:name");
                const char* content = get_value(root, "inv:content");
                const char* id      = get_value(root, "@id");
            
                if (id) {
                    LOG_INFO("🟢 SourcePlace: name=%s id=%s\n", name ? name : "(unnamed)", id);
                    insert_triple(db, block, id, "rdf:type", "inv:SourcePlace");
            
                    if (name)
                        insert_triple(db, block, id, "inv:name", name);
            
                    if (content)
                        insert_triple(db, block, id, "inv:content", content);
            
                    insert_triple(db, block, id, "context", json);
                }
            }
            

            if (is_type(root, "inv:DestinationPlace")) {
                matched = true;
            
                const char* name    = get_value(root, "inv:name");
                const char* content = get_value(root, "inv:content");  // Optional if you're writing output
                const char* id      = get_value(root, "@id");
            
                if (id) {
                    LOG_INFO("🔵 DestinationPlace: name=%s id=%s\n", name ? name : "(unnamed)", id);
                    insert_triple(db, block, id, "rdf:type", "inv:DestinationPlace");
            
                    if (name)
                        insert_triple(db, block, id, "inv:name", name);
            
                    if (content)
                        insert_triple(db, block, id, "inv:content", content);
            
                    insert_triple(db, block, id, "context", json);
                }
            }

            if (is_type(root, "inv:Expression")) {
                matched = true;
                const char* name = get_value(root, "inv:name");
                const char* id = get_value(root, "@id");
                if (!id) id = anon_id();
                delete_expression_triples(db, block, id);
                LOG_INFO("📘 Expression: name=%s id=%s\n", name, id);
                insert_triple(db, block, id, "rdf:type", "inv:Expression");
                if (name) insert_triple(db, block, id, "inv:name", name);
                insert_triple(db, block, id, "context", json);

                // --- NEW: source_list ---
                cJSON* src_list = cJSON_GetObjectItem(root, "inv:source_list");
                if (src_list && cJSON_IsArray(src_list)) {
                    for (int i = 0; i < cJSON_GetArraySize(src_list); i++) {
                        cJSON* src = cJSON_GetArrayItem(src_list, i);
                        const char* from = get_value(src, "@id");
                        const char* name = get_value(src, "inv:name");
                        if (from) {
                            LOG_INFO("🟢 [Expression] SourcePlace: name=%s from=%s\n", name ? name : "(unnamed)", from);
                            insert_triple(db, block, from, "rdf:type", "inv:SourcePlace");
                            if (name) insert_triple(db, block, from, "inv:name", name);
                        }
                    }
                }

                // --- NEW: destination_list ---
                cJSON* dst_list = cJSON_GetObjectItem(root, "inv:destination_list");
                if (dst_list && cJSON_IsArray(dst_list)) {
                    for (int i = 0; i < cJSON_GetArraySize(dst_list); i++) {
                        cJSON* dst = cJSON_GetArrayItem(dst_list, i);
                        const char* id = get_value(dst, "@id");
                        const char* name = get_value(dst, "inv:name");
                
                        if (id) {
                            LOG_INFO("🔵 [Expression] DestinationPlace: name=%s id=%s\n", name ? name : "(unnamed)", id);
                            insert_triple(db, block, id, "rdf:type", "inv:DestinationPlace");
                            if (name) insert_triple(db, block, id, "inv:name", name);
                        }
                    }
                }
                

                // --- NEW: embedded PlaceOfResolution ---
                cJSON* por = cJSON_GetObjectItem(root, "inv:place_of_resolution");
                if (por && cJSON_IsObject(por)) {
                    const char* por_id = get_value(por, "@id");
                    if (!por_id) por_id = anon_id();
                    LOG_INFO("🧠 [Expression] PlaceOfResolution: id=%s\n", por_id);
                    insert_triple(db, block, por_id, "rdf:type", "inv:PlaceOfResolution");
                    insert_triple(db, block, id, "inv:place_of_resolution", por_id);

                    cJSON* fragments = cJSON_GetObjectItem(por, "inv:hasExpressionFragment");
                    if (fragments && cJSON_IsArray(fragments)) {
                        int count = cJSON_GetArraySize(fragments);
                        for (int i = 0; i < count; i++) {
                            cJSON* fragment = cJSON_GetArrayItem(fragments, i);
                            if (!fragment || !cJSON_IsObject(fragment)) continue;
                        
                            const char* frag_id = get_value(fragment, "@id");
                            if (!frag_id) frag_id = anon_id();
                        
                            LOG_INFO("🔩 ExpressionFragment: %s\n", frag_id);
                            insert_triple(db, block, frag_id, "rdf:type", "inv:ExpressionFragment");
                            insert_triple(db, block, por_id, "inv:hasExpressionFragment", frag_id);
                        
                            const char* destinationPlace = get_value(fragment, "inv:destinationPlace");
                            const char* def = get_value(fragment, "inv:invokesDefinition");
                            const char* args = get_value(fragment, "inv:invocationArguments");
                        
                            if (destinationPlace) insert_triple(db, block, frag_id, "inv:destinationPlace", destinationPlace);
                            if (def) insert_triple(db, block, frag_id, "inv:invokesDefinition", def);
                            if (args) insert_triple(db, block, frag_id, "inv:invocationArguments", args);
                        
                            // ✅ Move this block INSIDE here 👇
                            cJSON* cond_inv = cJSON_GetObjectItem(fragment, "inv:hasConditionalInvocation");
                            if (cond_inv && cJSON_IsObject(cond_inv)) {
                                const char* cond_id = get_value(cond_inv, "@id");
                                if (!cond_id) cond_id = anon_id();
                        
                                LOG_INFO("🧠 ConditionalInvocation: %s\n", cond_id);
                                insert_triple(db, block, cond_id, "rdf:type", "inv:ConditionalInvocation");
                                insert_triple(db, block, frag_id, "inv:hasConditionalInvocation", cond_id);
                        
                                const char* invocationName = get_value(cond_inv, "inv:invocationName");
                                if (invocationName)
                                    insert_triple(db, block, cond_id, "inv:invocationName", invocationName);
                        
                                cJSON* out_list = cJSON_GetObjectItem(cond_inv, "inv:hasDestinationNames");
                                if (out_list && cJSON_IsArray(out_list)) {
                                    for (int j = 0; j < cJSON_GetArraySize(out_list); j++) {
                                        cJSON* out = cJSON_GetArrayItem(out_list, j);
                                        const char* out_id = get_value(out, "@id");
                                        const char* out_name = get_value(out, "inv:name");
                                        const char* out_content = get_value(out, "inv:content");
                        
                                        if (out_id) {
                                            insert_triple(db, block, out_id, "rdf:type", "inv:DestinationPlace");
                                            insert_triple(db, block, cond_id, "inv:hasDestinationNames", out_id);
                                            if (out_name) insert_triple(db, block, out_id, "inv:name", out_name);
                                            if (out_content) insert_triple(db, block, out_id, "inv:content", out_content);
                                        }
                                    }
                                }
                            }
                        }
                        
                    }
                }
            }

            if (is_type(root, "inv:Invocation")) {
                matched = true;
            
                const char* name = get_value(root, "inv:name");
                const char* id   = get_value(root, "@id");
                if (!id) id = anon_id();
            
                LOG_INFO("📘 Invocation: name=%s id=%s\n", name ? name : "(unnamed)", id);
                insert_triple(db, block, id, "rdf:type", "inv:Invocation");
                if (name) insert_triple(db, block, id, "inv:name", name);
                insert_triple(db, block, id, "context", json);
            
                // --- source_list ---
                cJSON* src_list = cJSON_GetObjectItem(root, "inv:source_list");
                if (src_list && cJSON_IsArray(src_list)) {
                    for (int i = 0; i < cJSON_GetArraySize(src_list); i++) {
                        cJSON* src = cJSON_GetArrayItem(src_list, i);
                        const char* sid = get_value(src, "@id");
                        const char* sname = get_value(src, "inv:name");
                        const char* val = get_value(src, "inv:hasContent");
            
                        if (sid) {
                            insert_triple(db, block, sid, "rdf:type", "inv:SourcePlace");
                            insert_triple(db, block, id, "inv:source_list", sid);
                            if (sname) insert_triple(db, block, sid, "inv:name", sname);
                            if (val)   insert_triple(db, block, sid, "inv:hasContent", val);
            
                            LOG_INFO("🟢 Invocation SourcePlace: %s = %s\n", sname ? sname : sid, val ? val : "(null)");
                        }
                    }
                }
            
                // --- destination_list ---
                cJSON* dst_list = cJSON_GetObjectItem(root, "inv:destination_list");
                if (dst_list && cJSON_IsArray(dst_list)) {
                    for (int i = 0; i < cJSON_GetArraySize(dst_list); i++) {
                        cJSON* dst = cJSON_GetArrayItem(dst_list, i);
                        const char* did = get_value(dst, "@id");
                        const char* dname = get_value(dst, "inv:name");
            
                        if (did) {
                            insert_triple(db, block, did, "rdf:type", "inv:DestinationPlace");
                            insert_triple(db, block, id, "inv:destination_list", did);
                            if (dname) insert_triple(db, block, did, "inv:name", dname);
            
                            LOG_INFO("🔵 Invocation DestinationPlace: %s (%s)\n", dname ? dname : did, did);
                        }
                    }
                }
            }
            


            free(json);
            if (!matched) {
                const char* type = get_value(root, "@type");
                if (type) {
                    LOG_WARN("⚠️ Unrecognized type: %s in file %s\n", type, entry->d_name);
                } else {
                    LOG_WARN("⚠️ No @type found in file %s\n", entry->d_name);
                }
            }
            
            cJSON_Delete(root);
        }
    }

    closedir(dir);
}