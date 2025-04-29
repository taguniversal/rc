#include "eval.h"
#include "log.h"
#include "mkrand.h"
#include "sqlite3.h"
#include "util.h"
#include <dirent.h>
#define MXML_INCLUDE_LEGACY_TYPES

#include <mxml.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

Definition *parse_definition(mxml_node_t *tree, const char *filename);

char *load_file(const char *filename) {
  FILE *file = fopen(filename, "rb");
  if (!file)
    return NULL;

  fseek(file, 0, SEEK_END);
  long len = ftell(file);
  rewind(file);

  char *buffer = malloc(len + 1);
  if (!buffer) {
    fclose(file);
    return NULL;
  }

  fread(buffer, 1, len, file);
  buffer[len] = '\0';
  fclose(file);
  return buffer;
}

DefinitionLibrary *create_definition_library() {
  DefinitionLibrary *lib =
      (DefinitionLibrary *)calloc(1, sizeof(DefinitionLibrary));
  if (!lib) {
    LOG_ERROR("❌ Failed to allocate DefinitionLibrary");
    return NULL;
  }
  return lib;
}

void add_blueprint(DefinitionLibrary *lib, const char *name,
                   const char *filepath) {
  if (!lib || !name || !filepath) {
    LOG_WARN("⚠️ Cannot add blueprint, missing data");
    return;
  }

  DefinitionBlueprint *bp =
      (DefinitionBlueprint *)calloc(1, sizeof(DefinitionBlueprint));
  bp->name = strdup(name);
  bp->filepath = strdup(filepath);

  lib->blueprints = (DefinitionBlueprint **)realloc(
      lib->blueprints,
      sizeof(DefinitionBlueprint *) * (lib->blueprint_count + 1));
  lib->blueprints[lib->blueprint_count] = bp;
  lib->blueprint_count++;
}

Definition *parse_definition_from_file(const char *filepath) {
  char *xml = load_file(filepath);
  if (!xml) {
    LOG_ERROR("❌ Cannot open definition file: %s", filepath);
    return NULL;
  }

  mxml_node_t *tree = mxmlLoadString(NULL, NULL, xml);
  free(xml);

  if (!tree) {
    LOG_ERROR("❌ Failed to parse XML in file: %s", filepath);
    return NULL;
  }

  Definition *def = parse_definition(tree, filepath);
  mxmlDelete(tree);

  return def;
}

void link_invocations_to_definitions(Block *blk, const char *inv_dir) {
  if (!blk || !inv_dir)
    return;

  char path[256];

  for (Invocation *inv = blk->invocations; inv; inv = inv->next) {
    snprintf(path, sizeof(path), "%s/%s.xml", inv_dir, inv->name);
    Definition *fresh_def = parse_definition_from_file(path);
    if (fresh_def) {
      inv->definition = fresh_def;
      LOG_INFO("🔗 Linked Invocation %s → fresh Definition from %s", inv->name,
               path);
    } else {
      LOG_WARN("⚠️ Could not load Definition file for Invocation: %s",
               inv->name);
    }
  }
}

void free_definition_library(DefinitionLibrary *lib) {
  if (!lib)
    return;

  for (size_t i = 0; i < lib->blueprint_count; i++) {
    free(lib->blueprints[i]->name);
    free(lib->blueprints[i]->filepath);
    free(lib->blueprints[i]);
  }
  free(lib->blueprints);
  free(lib);
}

Definition *instantiate_definition(const char *name, DefinitionLibrary *lib) {
  for (size_t i = 0; i < lib->blueprint_count; i++) {
    if (strcmp(name, lib->blueprints[i]->name) == 0) {
      return parse_definition_from_file(lib->blueprints[i]->filepath);
    }
  }
  return NULL;
}
int validate_xml_structure(mxml_node_t *tree) {
  if (!tree) {
    LOG_ERROR("❌ validate_xml_structure: NULL tree node passed in");
    return -1;
  }

  const char *tag = mxmlGetElement(tree);
  if (!tag) {
    LOG_ERROR("❌ validate_xml_structure: No top-level tag found");
    return -1;
  }

  if (!(strcmp(tag, "Definition") == 0 || strcmp(tag, "Invocation") == 0 ||
        strcmp(tag, "ArrayedExpression") == 0)) {
    LOG_ERROR("❌ Invalid root element: %s", tag);
    return -1;
  }

  LOG_INFO("✅ Root element: %s", tag);

  // === Check SourceList ===
  mxml_node_t *source_list =
      mxmlFindElement(tree, tree, "SourceList", NULL, NULL, MXML_DESCEND_FIRST);

  if (source_list) {
    LOG_INFO("🔎 Found SourceList");

    for (mxml_node_t *child = mxmlGetFirstChild(source_list); child;
         child = mxmlGetNextSibling(child)) {

      if (mxmlGetType(child) != MXML_TYPE_ELEMENT) {
        LOG_INFO("⚠️ Skipping non-element node in SourceList (type: %d)", mxmlGetType(child));
        continue;
      }

      const char *child_tag = mxmlGetElement(child);
      if (!child_tag || strcmp(child_tag, "SourcePlace") != 0) {
        LOG_ERROR("❌ Invalid element inside SourceList: %s (expected SourcePlace)",
                  child_tag ? child_tag : "(null)");
        return -1;
      }

      const char *from_attr = mxmlElementGetAttr(child, "from");
      const char *value_attr = mxmlElementGetAttr(child, "value");

      LOG_INFO("🔬 SourcePlace debug — from: '%s' | value: '%s'",
               from_attr ? from_attr : "NULL",
               value_attr ? value_attr : "NULL");

      if ((from_attr && value_attr) || (!from_attr && !value_attr)) {
        LOG_ERROR("❌ SourcePlace must have exactly one of 'from' or 'value'");
        return -1;
      }

      LOG_INFO("🧩 Valid SourcePlace: from='%s' value='%s'",
               from_attr ? from_attr : "NULL",
               value_attr ? value_attr : "NULL");
    }
  } else {
    LOG_INFO("ℹ️ No SourceList present (optional depending on type)");
  }

  // === Check DestinationList ===
  mxml_node_t *dest_list =
      mxmlFindElement(tree, tree, "DestinationList", NULL, NULL, MXML_DESCEND_FIRST);
  if (dest_list) {
    LOG_INFO("🔎 Found DestinationList");

    for (mxml_node_t *child = mxmlGetFirstChild(dest_list); child;
         child = mxmlGetNextSibling(child)) {

      if (mxmlGetType(child) != MXML_TYPE_ELEMENT) {
        LOG_INFO("⚠️ Skipping non-element node in DestinationList (type: %d)", mxmlGetType(child));
        continue;
      }

      const char *child_tag = mxmlGetElement(child);
      if (!child_tag || strcmp(child_tag, "DestinationPlace") != 0) {
        LOG_ERROR("❌ Invalid element inside DestinationList: %s (expected DestinationPlace)",
                  child_tag ? child_tag : "(null)");
        return -1;
      }

      LOG_INFO("📎 Valid DestinationPlace: %s",
               mxmlElementGetAttr(child, "name") ?: "(unnamed)");
    }
  } else {
    LOG_INFO("ℹ️ No DestinationList present (optional depending on type)");
  }

  LOG_INFO("✅ XML structure validated successfully");
  return 0;
}

ConditionalInvocation *parse_conditional_invocation(mxml_node_t *ci_node) {
  if (!ci_node)
    return NULL;

  ConditionalInvocation *ci =
      (ConditionalInvocation *)calloc(1, sizeof(ConditionalInvocation));
  if (!ci)
    return NULL;

  mxml_node_t *pattern_node = mxmlFindElement(
      ci_node, ci_node, "InvocationName", NULL, NULL, MXML_DESCEND_FIRST);
  if (pattern_node && mxmlGetOpaque(pattern_node)) {
    ci->invocation_template = strdup(mxmlGetOpaque(pattern_node));
    LOG_INFO("🧩 ConditionalInvocation pattern: %s", ci->invocation_template);
  }

  mxml_node_t *case_node =
      mxmlFindElement(ci_node, ci_node, "Case", NULL, NULL, MXML_DESCEND_FIRST);
  while (case_node) {
    const char *value_attr = mxmlElementGetAttr(case_node, "value");
    if (value_attr && mxmlGetOpaque(case_node)) {
      ConditionalInvocationCase *c = (ConditionalInvocationCase *)calloc(
          1, sizeof(ConditionalInvocationCase));
      c->pattern = strdup(value_attr);
      c->result = strdup(mxmlGetOpaque(case_node));
      c->next = ci->cases;
      ci->cases = c;

      LOG_INFO("🧩 Case added: %s ➔ %s", c->pattern, c->result);
    }
    case_node = mxmlFindElement(case_node, ci_node, "Case", NULL, NULL,
                                MXML_DESCEND_FIRST);
  }

  return ci;
}

void parse_sources(mxml_node_t *parent, void **list_head, bool is_definition) {
  mxml_node_t *src = mxmlFindElement(parent, parent, "SourcePlace", NULL, NULL,
                                     MXML_DESCEND_FIRST);

  while (src) {
    if (is_definition) {
      const char *name_attr = mxmlElementGetAttr(src, "name");
      if (!name_attr) {
        LOG_ERROR("❌ Definition SourcePlace missing 'name' attribute");
        return;
      }
      SourcePlace *place = (SourcePlace *)calloc(1, sizeof(SourcePlace));
      place->name = strdup(name_attr);
      place->signal = &NULL_SIGNAL;
      place->next = *(SourcePlace **)list_head;
      *(SourcePlace **)list_head = place;
      LOG_INFO("📎 Definition source added: %s", place->name);

    } else {
      const char *from_attr = mxmlElementGetAttr(src, "from");
      const char *value_attr = mxmlElementGetAttr(src, "value");

      if (value_attr) {
        // Literal signal
        Signal *sig = (Signal *)calloc(1, sizeof(Signal));
        sig->content = strdup(value_attr);

        SourcePlace *place = (SourcePlace *)calloc(1, sizeof(SourcePlace));
        place->name = NULL; // No name for literal
        place->signal = sig;
        place->next = *(SourcePlace **)list_head;
        *(SourcePlace **)list_head = place;

        LOG_INFO("💎 Invocation source (literal) added: [%s]", value_attr);

      } else if (from_attr) {
        // Referenced signal (future feature if you want)
        SourcePlace *place = (SourcePlace *)calloc(1, sizeof(SourcePlace));
        place->name = strdup(from_attr);
        place->signal = &NULL_SIGNAL;
        place->next = *(SourcePlace **)list_head;
        *(SourcePlace **)list_head = place;

        LOG_INFO("🔗 Invocation source (from): %s", from_attr);

      } else {
        LOG_ERROR(
            "❌ Invocation SourcePlace must have either 'from' or 'value'");
        return;
      }
    }

    // ✅ Always advance to next element, whether value or from
    src = mxmlFindElement(src, parent, "SourcePlace", NULL, NULL,
                          MXML_DESCEND_ALL);
  }
}

void parse_destinations(mxml_node_t *parent, void **list_head,
                        bool is_definition) {
  mxml_node_t *dst = mxmlFindElement(parent, parent, "DestinationPlace", NULL,
                                     NULL, MXML_DESCEND_FIRST);

  while (dst) {
    const char *name_attr = mxmlElementGetAttr(dst, "name");
    if (!name_attr) {
      LOG_ERROR("❌ DestinationPlace missing 'name' attribute");
      return;
    }

    if (is_definition) {
      DestinationPlace *place =
          (DestinationPlace *)calloc(1, sizeof(DestinationPlace));
      place->name = strdup(name_attr);
      place->signal = &NULL_SIGNAL;
      place->next = *(DestinationPlace **)list_head;
      *(DestinationPlace **)list_head = place;
      LOG_INFO("📎 Definition destination added: %s", place->name);

    } else {
      Signal *sig = (Signal *)calloc(1, sizeof(Signal));
      sig->name = strdup(name_attr);
      sig->content = NULL; // It will be filled by evaluation later
      sig->next = *(Signal **)list_head;
      *(Signal **)list_head = sig;
      LOG_INFO("📎 Invocation destination added: %s", sig->name);
    }

    // 🔥 Correct walking now
    dst = mxmlFindElement(dst, parent, "DestinationPlace", NULL, NULL,
                          MXML_DESCEND_ALL);
  }
}

Definition *parse_definition(mxml_node_t *tree, const char *filename) {
  if (!tree)
    return NULL;

  Definition *def = (Definition *)calloc(1, sizeof(Definition));
  if (!def) {
    LOG_ERROR("❌ Failed to allocate Definition structure for %s", filename);
    return NULL;
  }

  mxml_node_t *name_node =
      mxmlFindElement(tree, tree, "Name", NULL, NULL, MXML_DESCEND_FIRST);
  if (name_node) {
    const char *text = mxmlGetText(name_node, NULL);
    if (text) {
      def->name = strdup(text);
      LOG_INFO("🔖 Definition name: %s", def->name);
    }
  } else {
    def->name = strdup(filename);
    LOG_WARN("⚠️ No name found for %s, using filename fallback", filename);
  }
  // 2. Parse SourceList
  mxml_node_t *source_list =
      mxmlFindElement(tree, tree, "SourceList", NULL, NULL, MXML_DESCEND_FIRST);
  if (source_list) {
    parse_sources(source_list, (void **)&def->sources, true);
  } else {
    LOG_WARN("⚠️ No SourceList found for %s", filename);
  }

  mxml_node_t *dest_list = mxmlFindElement(tree, tree, "DestinationList", NULL,
                                           NULL, MXML_DESCEND_FIRST);
  if (dest_list) {
    parse_destinations(dest_list, (void **)&def->destinations, true);
  } else {
    LOG_WARN("⚠️ No DestinationList found for %s", filename);
  }

  // 4. Parse ConditionalInvocation if present
  mxml_node_t *ci = mxmlFindElement(tree, tree, "ConditionalInvocation", NULL,
                                    NULL, MXML_DESCEND_FIRST);
  if (ci) {
    def->conditional_invocation = parse_conditional_invocation(ci);
    if (def->conditional_invocation) {
      LOG_INFO("🔎 ConditionalInvocation loaded for %s", def->name);
    }
  } else {
    LOG_WARN("⚠️ No ConditionalInvocation found for %s", filename);
  }

  return def;
}

Invocation *parse_invocation(mxml_node_t *tree, const char *filename) {
  if (!tree)
    return NULL;

  Invocation *inv = (Invocation *)calloc(1, sizeof(Invocation));
  if (!inv) {
    LOG_ERROR("❌ Failed to allocate Invocation for %s", filename);
    return NULL;
  }

  // 1. Get name
  mxml_node_t *name_node =
      mxmlFindElement(tree, tree, "Name", NULL, NULL, MXML_DESCEND_FIRST);
  if (name_node) {
    const char *text = mxmlGetText(name_node, NULL);
    if (text) {
      inv->name = strdup(text);
      LOG_INFO("🔖 Invocation name: %s", inv->name);
    }
  } else {
    inv->name = strdup(filename);
    LOG_WARN("⚠️ No name found for %s, using filename fallback", filename);
  }

  // 2. Parse SourceList
  mxml_node_t *source_list =
      mxmlFindElement(tree, tree, "SourceList", NULL, NULL, MXML_DESCEND_FIRST);
  if (source_list) {
    parse_sources(source_list, (void **)&inv->sources, false);
  } else {
    LOG_WARN("⚠️ No SourceList found for Invocation %s", inv->name);
  }

  // 3. Parse DestinationList
  mxml_node_t *dest_list = mxmlFindElement(tree, tree, "DestinationList", NULL,
                                           NULL, MXML_DESCEND_FIRST);
  if (dest_list) {
    parse_destinations(dest_list, (void **)&inv->destinations,
                       false); // false = DestinationPlace
  } else {
    LOG_WARN("⚠️ No DestinationList found for Invocation %s", inv->name);
  }

  return inv;
}

void parse_block_from_xml(Block *blk, const char *inv_dir) {
  DIR *dir = opendir(inv_dir);
  if (!dir) {
    LOG_ERROR("❌ Unable to open invocation directory: %s\n", inv_dir);
    return;
  }

  LOG_INFO("🔍 Parsing block contents from directory: %s", inv_dir);

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strstr(entry->d_name, ".xml")) {
      char path[256];
      snprintf(path, sizeof(path), "%s/%s", inv_dir, entry->d_name);

      LOG_INFO("📄 Loading XML: %s", path);
      char *xml = load_file(path);
      if (!xml) {
        LOG_WARN("⚠️ Failed to read file: %s", path);
        continue;
      }

      mxml_node_t *tree = mxmlLoadString(NULL, NULL, xml);
      if (!tree) {
        LOG_WARN("⚠️ Failed to parse XML tree: %s", path);
        free(xml);
        continue;
      }

      if (validate_xml_structure(tree) != 0) {
        LOG_WARN("⚠️ Invalid XML structure in %s, skipping", path);
        mxmlDelete(tree);
        free(xml);
        continue;
      }

      const char *top_tag = mxmlGetElement(tree);
      const char *file_basename = entry->d_name;

      if (strcmp(top_tag, "Definition") == 0) {
        Definition *def = parse_definition(tree, file_basename);
        if (def) {
          def->next = blk->definitions;
          blk->definitions = def;
          LOG_INFO("📦 Added Definition: %s", def->name);
        }
      } else if (strcmp(top_tag, "Invocation") == 0) {
        Invocation *inv = parse_invocation(tree, file_basename);
        if (inv) {
          inv->next = blk->invocations;
          blk->invocations = inv;
          LOG_INFO("📦 Added Invocation: %s", inv->name);
        }
      } else {
        LOG_WARN("⚠️ Unknown top-level element: %s in file %s", top_tag,
                 entry->d_name);
      }

      for (Invocation *inv = blk->invocations; inv; inv = inv->next) {
        char def_path[256];
        snprintf(def_path, sizeof(def_path), "%s/%s.xml", inv_dir, inv->name);
        inv->definition = parse_definition_from_file(def_path);

        if (inv->definition) {
          LOG_INFO("🔗 Linked Invocation %s → Definition %s", inv->name,
                   inv->definition->name);
        } else {
          LOG_WARN("⚠️ Could not load Definition file for Invocation %s",
                   inv->name);
        }
      }

      mxmlDelete(tree);
      free(xml);
    }
  }

  closedir(dir);

  LOG_INFO("✅ Block population from XML completed.");
}

// Memory freeing functions
void free_signal(Signal *sig) {
  while (sig) {
    Signal *next = sig->next;
    if (sig->name)
      free(sig->name);
    if (sig->content)
      free(sig->content);
    free(sig);
    sig = next;
  }
}

void free_source_places(SourcePlace *src) {
  while (src) {
    SourcePlace *next = src->next;
    if (src->name)
      free(src->name);
    free_signal(src->signal);
    free(src);
    src = next;
  }
}

void free_destination_places(DestinationPlace *dst) {
  while (dst) {
    DestinationPlace *next = dst->next;
    if (dst->name)
      free(dst->name);
    free_signal(dst->signal);
    free(dst);
    dst = next;
  }
}

static void free_conditional_invocation(ConditionalInvocation *ci) {
  if (!ci)
    return;
  if (ci->invocation_template)
    free(ci->invocation_template);

  ConditionalInvocationCase *c = ci->cases;
  while (c) {
    ConditionalInvocationCase *next = c->next;
    if (c->pattern)
      free(c->pattern);
    if (c->result)
      free(c->result);
    free(c);
    c = next;
  }
  free(ci);
}

static void free_definitions(Definition *def) {
  while (def) {
    Definition *next = def->next;
    if (def->name)
      free(def->name);
    free_source_places(def->sources);
    free_destination_places(def->destinations);
    free_conditional_invocation(def->conditional_invocation);
    free(def);
    def = next;
  }
}

static void free_invocations(Invocation *inv) {
  while (inv) {
    Invocation *next = inv->next;
    if (inv->name)
      free(inv->name);
    free_source_places(inv->sources);
    free_destination_places(inv->destinations);
    free(inv);
    inv = next;
  }
}

void free_block(Block *blk) {
  if (!blk)
    return;
  if (blk->psi)
    free(blk->psi);
  free_definitions(blk->definitions);
  free_invocations(blk->invocations);
  free(blk);
}
