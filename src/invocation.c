#include "eval.h"
#include "log.h"
#include "mkrand.h"
#include "rdf.h"
#include "sqlite3.h"
#include "util.h"
#include <dirent.h>
#include <mxml.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

void process_definition(sqlite3 *db, const char *block, mxml_node_t *tree,
                        const char *xml, const char *name) {
  LOG_INFO("⚙️ Processing Definition: %s", name);

  // Find <SourceList>
  mxml_node_t *source_list =
      mxmlFindElement(tree, tree, "SourceList", NULL, NULL, MXML_DESCEND_ALL);
  if (source_list) {
    LOG_INFO("🔎 Found SourceList for %s", name);

    for (mxml_node_t *src = mxmlFindElement(source_list, source_list, "source",
                                            NULL, NULL, MXML_DESCEND_ALL);
         src; src = mxmlFindElement(src, source_list, "source", NULL, NULL,
                                    MXML_DESCEND_FIRST)) {

      const char *src_name = mxmlElementGetAttr(src, "name");
      if (src_name) {
        LOG_INFO("📎 Source: %s", src_name);
        // TODO: Insert source into database or local structure
      }
    }
  } else {
    LOG_WARN("⚠️ No SourceList found for %s", name);
  }

  // Find <DestinationList>
  mxml_node_t *dest_list = mxmlFindElement(tree, tree, "DestinationList", NULL,
                                           NULL, MXML_DESCEND_ALL);
  if (dest_list) {
    LOG_INFO("🔎 Found DestinationList for %s", name);

    for (mxml_node_t *dst = mxmlFindElement(dest_list, dest_list, "destination",
                                            NULL, NULL, MXML_DESCEND_ALL);
         dst; dst = mxmlFindElement(dst, dest_list, "destination", NULL, NULL,
                                    MXML_DESCEND_FIRST)) {

      const char *dst_name = mxmlElementGetAttr(dst, "name");
      if (dst_name) {
        LOG_INFO("📎 Destination: %s", dst_name);
        // TODO: Insert destination into database or local structure
      }
    }
  } else {
    LOG_WARN("⚠️ No DestinationList found for %s", name);
  }

  // Find <ConditionalInvocation>
  mxml_node_t *conditional_inv = mxmlFindElement(
      tree, tree, "ConditionalInvocation", NULL, NULL, MXML_DESCEND_ALL);
  if (conditional_inv) {
    LOG_INFO("🔎 Found ConditionalInvocation for %s", name);

    // Example: pull out invocationName
    mxml_node_t *inv_name_node =
        mxmlFindElement(conditional_inv, conditional_inv, "invocationName",
                        NULL, NULL, MXML_DESCEND_ALL);
    if (inv_name_node && mxmlGetOpaque(inv_name_node)) {
      const char *invocationName = mxmlGetOpaque(inv_name_node);
      LOG_INFO("📎 Invocation Pattern: %s", invocationName);
      // TODO: Store or process invocation pattern
    }

    // Loop over <case> entries
    for (mxml_node_t *case_node =
             mxmlFindElement(conditional_inv, conditional_inv, "case", NULL,
                             NULL, MXML_DESCEND_ALL);
         case_node;
         case_node = mxmlFindElement(case_node, conditional_inv, "case", NULL,
                                     NULL, MXML_DESCEND_FIRST)) {

      const char *case_value = mxmlElementGetAttr(case_node, "value");
      const char *case_result = mxmlGetOpaque(case_node);
      if (case_value && case_result) {
        LOG_INFO("📎 Case: %s -> %s", case_value, case_result);
        // TODO: Store or process conditional case
      }
    }
  } else {
    LOG_WARN("⚠️ No ConditionalInvocation found for %s", name);
  }

  // (Optional: you can add more fields later, like meta, description,
  // etc.)
}

void process_invocation(sqlite3 *db, const char *block, mxml_node_t *tree,
                        const char *xml, const char *name) {
  LOG_INFO("⚙️ Processing Invocation: %s", name);

  // Find <SourceList>
  mxml_node_t *source_list =
      mxmlFindElement(tree, tree, "SourceList", NULL, NULL, MXML_DESCEND_ALL);
  if (source_list) {
    LOG_INFO("🔎 Found SourceList for %s", name);

    for (mxml_node_t *src = mxmlFindElement(source_list, source_list, "source",
                                            NULL, NULL, MXML_DESCEND_ALL);
         src; src = mxmlFindElement(src, source_list, "source", NULL, NULL,
                                    MXML_DESCEND_FIRST)) {

      const char *src_name = mxmlElementGetAttr(src, "name");
      if (src_name) {
        LOG_INFO("📎 Source: %s", src_name);
        // TODO: Insert source into local structure
      }
    }
  } else {
    LOG_WARN("⚠️ No SourceList found for %s", name);
  }

  // Find <DestinationList>
  mxml_node_t *dest_list = mxmlFindElement(tree, tree, "DestinationList", NULL,
                                           NULL, MXML_DESCEND_ALL);
  if (dest_list) {
    LOG_INFO("🔎 Found DestinationList for %s", name);

    for (mxml_node_t *dst = mxmlFindElement(dest_list, dest_list, "destination",
                                            NULL, NULL, MXML_DESCEND_ALL);
         dst; dst = mxmlFindElement(dst, dest_list, "destination", NULL, NULL,
                                    MXML_DESCEND_FIRST)) {

      const char *dst_name = mxmlElementGetAttr(dst, "name");
      if (dst_name) {
        LOG_INFO("📎 Destination: %s", dst_name);
        // TODO: Insert destination into local structure
      }
    }
  } else {
    LOG_WARN("⚠️ No DestinationList found for %s", name);
  }

  // (Optional: You can add more fields later if invocations grow more complex.)
}
int validate_xml_structure(mxml_node_t *tree) {
  if (!tree)
    return -1;

  const char *tag = mxmlGetElement(tree);
  if (!tag)
    return -1;

  // Allow only certain root elements
  if (!(strcmp(tag, "Definition") == 0 || strcmp(tag, "Invocation") == 0 ||
        strcmp(tag, "ArrayedExpression") == 0)) {
    LOG_ERROR("❌ Invalid root element: %s", tag);
    return -1;
  }

  // === Check SourceList ===
  mxml_node_t *source_list =
      mxmlFindElement(tree, tree, "SourceList", NULL, NULL, MXML_DESCEND_FIRST);
  // Check SourceList contents
  if (source_list) {
    for (mxml_node_t *child = mxmlGetFirstChild(source_list); child;
         child = mxmlGetNextSibling(child)) {

      if (strcmp(mxmlGetElement(child), "SourcePlace") != 0) {
        LOG_ERROR(
            "❌ Invalid element inside SourceList: %s (expected SourcePlace)",
            mxmlGetElement(child));
        return -1;
      }

      const char *from_attr = mxmlElementGetAttr(child, "from");
      const char *value_attr = mxmlElementGetAttr(child, "value");

      if ((from_attr && value_attr) || (!from_attr && !value_attr)) {
        LOG_ERROR("❌ SourcePlace must have exactly one of 'from' or 'value'");
        return -1;
      }
    }
  }

  // === Check DestinationList ===
  mxml_node_t *dest_list = mxmlFindElement(tree, tree, "DestinationList", NULL,
                                           NULL, MXML_DESCEND_FIRST);
  if (dest_list) {
    for (mxml_node_t *child = mxmlGetFirstChild(dest_list); child;
         child = mxmlGetNextSibling(child)) {
      if (strcmp(mxmlGetElement(child), "DestinationPlace") != 0) {
        LOG_ERROR("❌ Invalid element inside DestinationList: %s (expected "
                  "DestinationPlace)",
                  mxmlGetElement(child));
        return -1;
      }
    }
  }

  return 0; // valid
}

ConditionalInvocation *parse_conditional_invocation(mxml_node_t *ci_node) {
  if (!ci_node)
    return NULL;

  ConditionalInvocation *ci =
      (ConditionalInvocation *)calloc(1, sizeof(ConditionalInvocation));
  if (!ci)
    return NULL;

  mxml_node_t *pattern_node = mxmlFindElement(
      ci_node, ci_node, "invocationName", NULL, NULL, MXML_DESCEND_FIRST);
  if (pattern_node && mxmlGetOpaque(pattern_node)) {
    ci->invocation_template = strdup(mxmlGetOpaque(pattern_node));
    LOG_INFO("🧩 ConditionalInvocation pattern: %s", ci->invocation_template);
  }

  mxml_node_t *case_node =
      mxmlFindElement(ci_node, ci_node, "case", NULL, NULL, MXML_DESCEND_FIRST);
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
    case_node = mxmlFindElement(case_node, ci_node, "case", NULL, NULL,
                                MXML_DESCEND_FIRST);
  }

  return ci;
}

void parse_sources(mxml_node_t *parent, void **list_head, bool is_definition) {
  mxml_node_t *src = mxmlFindElement(
      parent, parent, is_definition ? "SourcePlace" : "SourcePlace", NULL, NULL,
      MXML_DESCEND_FIRST);

  while (src) {
    const char *from_attr = mxmlElementGetAttr(src, "from");
    const char *value_attr = mxmlElementGetAttr(src, "value");

    if (is_definition) {
      // For Definitions, SourcePlace only has a name
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
      // For Invocations, SourcePlace has either from= or value=
      if ((from_attr && value_attr) || (!from_attr && !value_attr)) {
        LOG_ERROR("❌ Invocation SourcePlace must have exactly one of 'from' "
                  "or 'value'");
        return;
      }

      SourcePlace *place = (SourcePlace *)calloc(1, sizeof(SourcePlace));
      if (from_attr) {
        place->name = strdup(from_attr);
        place->value = NULL;
        LOG_INFO("🔗 Invocation source (link): %s", place->name);
      } else {
        place->name = NULL;
        place->value = strdup(value_attr);
        LOG_INFO("💎 Invocation source (literal): %s", place->value);
      }
      place->signal = &NULL_SIGNAL;
      place->next = *(SourcePlace **)list_head;
      *(SourcePlace **)list_head = place;
    }

    src = mxmlFindElement(src, parent, "SourcePlace", NULL, NULL,
                          MXML_DESCEND_FIRST);
  }
}

void parse_destinations(mxml_node_t *parent, void **list_head,
                        bool is_definition) {
  mxml_node_t *dst = mxmlFindElement(
      parent, parent, is_definition ? "DestinationPlace" : "DestinationPlace",
      NULL, NULL, MXML_DESCEND_FIRST);
  while (dst) {
    const char *name_attr = mxmlElementGetAttr(dst, "name");
    if (name_attr) {
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
        sig->next = *(Signal **)list_head;
        *(Signal **)list_head = sig;
        LOG_INFO("📎 Invocation destination added: %s", sig->name);
      }
    }
    dst = mxmlFindElement(
        dst, parent, is_definition ? "DestinationPlace" : "DestinationPlace",
        NULL, NULL, MXML_DESCEND_FIRST);
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
      mxmlFindElement(tree, tree, "name", NULL, NULL, MXML_DESCEND_FIRST);
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
      mxmlFindElement(tree, tree, "name", NULL, NULL, MXML_DESCEND_FIRST);
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

      mxmlDelete(tree);
      free(xml);
    }
  }

  closedir(dir);

  LOG_INFO("✅ Block population from XML completed.");
}

void map_io(Block *blk, const char *inv_dir) {
  DIR *dir = opendir(inv_dir);

  LOG_INFO("\xF0\x9F\x94\x8D map_io starting. in %s\n", inv_dir);

  if (!dir) {
    LOG_ERROR("❌ '%s' directory not found. Skipping map_io.", inv_dir);
    return;
  }

  parse_block_from_xml(blk, inv_dir);

  closedir(dir);
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
