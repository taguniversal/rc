#include "eval.h"
#include "log.h"
#include "mkrand.h"
#include "sqlite3.h"
#include "util.h"
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

Invocation *parse_invocation(xmlNodePtr tree, const char *filename);
Definition *parse_definition(xmlNodePtr tree, const char *filename);

char *load_file(const char *filename)
{
  FILE *file = fopen(filename, "rb");
  if (!file)
    return NULL;

  fseek(file, 0, SEEK_END);
  long len = ftell(file);
  rewind(file);

  char *buffer = malloc(len + 1);
  if (!buffer)
  {
    fclose(file);
    return NULL;
  }

  fread(buffer, 1, len, file);
  buffer[len] = '\0';
  fclose(file);
  return buffer;
}

void parse_file(const char *filename)
{
  xmlDoc *doc = xmlReadFile(filename, NULL, 0);
  if (!doc)
  {
    fprintf(stderr, "❌ Failed to parse %s\n", filename);
    return;
  }

  xmlNode *root = xmlDocGetRootElement(doc);
  for (xmlNode *node = root; node; node = node->next)
  {
    if (node->type == XML_ELEMENT_NODE)
    {
      printf("Element name: %s\n", node->name);
    }
  }

  xmlFreeDoc(doc);
}

DefinitionLibrary *create_definition_library()
{
  DefinitionLibrary *lib =
      (DefinitionLibrary *)calloc(1, sizeof(DefinitionLibrary));
  if (!lib)
  {
    LOG_ERROR("❌ Failed to allocate DefinitionLibrary");
    return NULL;
  }
  return lib;
}

void add_blueprint(DefinitionLibrary *lib, const char *name,
                   const char *filepath)
{
  if (!lib || !name || !filepath)
  {
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

Definition *parse_definition_from_file(const char *filepath)
{
  char *xml = load_file(filepath);
  if (!xml)
  {
    LOG_ERROR("❌ Cannot open definition file: %s", filepath);
    return NULL;
  }

  // Parse XML string into a libxml2 document
  xmlDocPtr doc = xmlReadMemory(xml, strlen(xml), filepath, NULL, 0);
  free(xml);

  if (!doc)
  {
    LOG_ERROR("❌ Failed to parse XML in file: %s", filepath);
    return NULL;
  }

  // Get root element (this replaces mxmlLoadString behavior)
  xmlNodePtr root = xmlDocGetRootElement(doc);

  if (!root)
  {
    LOG_ERROR("❌ No root element in XML file: %s", filepath);
    xmlFreeDoc(doc);
    return NULL;
  }

  // Pass root node (instead of whole tree) to parse_definition
  Definition *def = parse_definition(root, filepath);

  xmlFreeDoc(doc); // Clean up doc after parsing
  return def;
}

void link_invocations_to_definitions(Block *blk, const char *inv_dir)
{
  if (!blk || !inv_dir)
    return;

  char path[256];

  for (Invocation *inv = blk->invocations; inv; inv = inv->next)
  {
    snprintf(path, sizeof(path), "%s/%s.xml", inv_dir, inv->name);
    Definition *fresh_def = parse_definition_from_file(path);
    if (fresh_def)
    {
      inv->definition = fresh_def;
      LOG_INFO("🔗 Linked Invocation %s → fresh Definition from %s", inv->name,
               path);
    }
    else
    {
      LOG_WARN("⚠️ Could not load Definition file for Invocation: %s",
               inv->name);
    }
  }
}

void free_definition_library(DefinitionLibrary *lib)
{
  if (!lib)
    return;

  for (size_t i = 0; i < lib->blueprint_count; i++)
  {
    free(lib->blueprints[i]->name);
    free(lib->blueprints[i]->filepath);
    free(lib->blueprints[i]);
  }
  free(lib->blueprints);
  free(lib);
}

Definition *instantiate_definition(const char *name, DefinitionLibrary *lib)
{
  for (size_t i = 0; i < lib->blueprint_count; i++)
  {
    if (strcmp(name, lib->blueprints[i]->name) == 0)
    {
      return parse_definition_from_file(lib->blueprints[i]->filepath);
    }
  }
  return NULL;
}
#include <libxml/parser.h>
#include <libxml/tree.h>

ConditionalInvocation *parse_conditional_invocation(xmlNodePtr ci_node)
{
  if (!ci_node)
    return NULL;

  ConditionalInvocation *ci = calloc(1, sizeof(ConditionalInvocation));
  if (!ci)
    return NULL;

  // Iterate over children
  for (xmlNodePtr child = ci_node->children; child; child = child->next)
  {
    if (child->type == XML_ELEMENT_NODE)
    {
      if (xmlStrcmp(child->name, (const xmlChar *)"InvocationName") == 0)
      {
        xmlChar *text = xmlNodeGetContent(child);
        if (text)
        {
          ci->invocation_template = strdup((const char *)text);
          LOG_INFO("🧩 ConditionalInvocation pattern: %s", ci->invocation_template);
          xmlFree(text);
        }
      }
      else if (xmlStrcmp(child->name, (const xmlChar *)"Case") == 0)
      {
        xmlChar *value_attr = xmlGetProp(child, (const xmlChar *)"value");
        xmlChar *result_text = xmlNodeGetContent(child);

        if (value_attr && result_text)
        {
          ConditionalInvocationCase *c = calloc(1, sizeof(ConditionalInvocationCase));
          if (!c)
            break;

          char *trimmed = strdup((const char *)value_attr);
          if (trimmed)
          {
            trimmed[strcspn(trimmed, "\r\n")] = '\0'; // trim \r\n
            c->pattern = trimmed;
          }

          c->result = strdup((const char *)result_text);
          c->next = ci->cases;
          ci->cases = c;

          LOG_INFO("🧩 Case added: %s ➔ %s", c->pattern, c->result);
        }

        if (value_attr)
          xmlFree(value_attr);
        if (result_text)
          xmlFree(result_text);
      }
    }
  }

  if (!ci->cases)
  {
    LOG_WARN("⚠️ ConditionalInvocation contains no cases!");
  }

  return ci;
}

void parse_sources(xmlNodePtr list, void **dest, bool is_definition)
{
  SourcePlace *head = NULL, *tail = NULL;

  for (xmlNodePtr node = list->children; node; node = node->next)
  {
    if (node->type != XML_ELEMENT_NODE || xmlStrcmp(node->name, (const xmlChar *)"SourcePlace") != 0)
      continue;

    SourcePlace *sp = (SourcePlace *)calloc(1, sizeof(SourcePlace));
    if (!sp)
      continue;

    xmlChar *name_attr = xmlGetProp(node, (const xmlChar *)"name");
    xmlChar *value_attr = xmlGetProp(node, (const xmlChar *)"value");

    if (name_attr && strlen((const char *)name_attr) > 0)
    {
      sp->name = strdup((const char *)name_attr);
    }
    else if (value_attr && strlen((const char *)value_attr) > 0)
    {
      sp->value = strdup((const char *)value_attr);

      // ✅ Initialize signal object
      sp->signal = (Signal *)calloc(1, sizeof(Signal));
      if (!sp->signal)
      {
        fprintf(stderr, "❌ parse_sources: Failed to allocate Signal\n");
        abort();
      }

      sp->signal->content = strdup((const char *)value_attr);
    }

    xmlFree(name_attr);
    xmlFree(value_attr);

    sp->next = NULL;
    if (!head)
    {
      head = tail = sp;
    }
    else
    {
      tail->next = sp;
      tail = sp;
    }
  }

  *dest = head;
}

void parse_destinations(xmlNodePtr parent, void **list_head, bool is_definition)
{
  if (!parent)
    return;

  for (xmlNodePtr dst = parent->children; dst; dst = dst->next)
  {
    if (dst->type != XML_ELEMENT_NODE)
      continue;
    if (xmlStrcmp(dst->name, (const xmlChar *)"DestinationPlace") != 0)
      continue;

    xmlChar *name_attr = xmlGetProp(dst, (const xmlChar *)"name");
    if (!name_attr)
    {
      LOG_ERROR("❌ DestinationPlace missing 'name' attribute");
      return;
    }

    if (is_definition)
    {
      DestinationPlace *place = calloc(1, sizeof(DestinationPlace));
      place->name = strdup((const char *)name_attr);
      place->signal = &NULL_SIGNAL;
      place->next = *(DestinationPlace **)list_head;
      *(DestinationPlace **)list_head = place;
      LOG_INFO("📎 Definition destination added: %s", place->name);
    }
    else
    {
      Signal *sig = calloc(1, sizeof(Signal));
      sig->name = strdup((const char *)name_attr);
      sig->content = NULL; // Will be filled later
      sig->next = *(Signal **)list_head;
      *(Signal **)list_head = sig;
      LOG_INFO("📎 Invocation destination added: %s", sig->name);
    }

    xmlFree(name_attr);
  }
}
Definition *parse_definition(xmlNodePtr tree, const char *filename)
{
  if (!tree)
    return NULL;

  Definition *def = calloc(1, sizeof(Definition));
  if (!def)
  {
    LOG_ERROR("❌ Failed to allocate Definition structure for %s", filename);
    return NULL;
  }

  // === Find <Name> node
  xmlNodePtr name_node = NULL;
  for (xmlNodePtr child = tree->children; child; child = child->next)
  {
    if (child->type == XML_ELEMENT_NODE && xmlStrcmp(child->name, (const xmlChar *)"Name") == 0)
    {
      name_node = child;
      break;
    }
  }

  if (name_node)
  {
    xmlChar *text = xmlNodeGetContent(name_node);
    if (text)
    {
      def->name = strdup((const char *)text);
      xmlFree(text);
      LOG_INFO("🔖 Definition name: %s", def->name);
    }
  }
  else
  {
    def->name = strdup(filename);
    LOG_WARN("⚠️ No name found for %s, using filename fallback", filename);
  }

  // === Find <SourceList>
  xmlNodePtr source_list = NULL;
  for (xmlNodePtr child = tree->children; child; child = child->next)
  {
    if (child->type == XML_ELEMENT_NODE && xmlStrcmp(child->name, (const xmlChar *)"SourceList") == 0)
    {
      source_list = child;
      break;
    }
  }
  if (source_list)
  {
    parse_sources(source_list, (void **)&def->sources, true);
  }
  else
  {
    LOG_WARN("⚠️ No SourceList found for %s", filename);
  }

  // === Find <DestinationList>
  xmlNodePtr dest_list = NULL;
  for (xmlNodePtr child = tree->children; child; child = child->next)
  {
    if (child->type == XML_ELEMENT_NODE && xmlStrcmp(child->name, (const xmlChar *)"DestinationList") == 0)
    {
      dest_list = child;
      break;
    }
  }
  if (dest_list)
  {
    parse_destinations(dest_list, (void **)&def->destinations, true);
  }
  else
  {
    LOG_WARN("⚠️ No DestinationList found for %s", filename);
  }

  // === Find <ConditionalInvocation>
  xmlNodePtr ci_node = NULL;
  for (xmlNodePtr child = tree->children; child; child = child->next)
  {
    if (child->type == XML_ELEMENT_NODE && xmlStrcmp(child->name, (const xmlChar *)"ConditionalInvocation") == 0)
    {
      ci_node = child;
      break;
    }
  }
  if (ci_node)
  {
    def->conditional_invocation = parse_conditional_invocation(ci_node);
    if (def->conditional_invocation)
    {
      LOG_INFO("🔎 ConditionalInvocation loaded for %s", def->name);
    }
  }
  else
  {
    LOG_WARN("⚠️ No ConditionalInvocation found for %s", filename);
  }

  return def;
}

Invocation *parse_invocation(xmlNodePtr tree, const char *filename)
{
  if (!tree)
    return NULL;

  Invocation *inv = (Invocation *)calloc(1, sizeof(Invocation));
  if (!inv)
  {
    LOG_ERROR("❌ Failed to allocate Invocation for %s", filename);
    return NULL;
  }

  // === 1. Find <Name>
  xmlNodePtr name_node = NULL;
  for (xmlNodePtr child = tree->children; child; child = child->next)
  {
    if (child->type == XML_ELEMENT_NODE && xmlStrcmp(child->name, (const xmlChar *)"Name") == 0)
    {
      name_node = child;
      break;
    }
  }

  if (name_node)
  {
    xmlChar *text = xmlNodeGetContent(name_node);
    if (text)
    {
      inv->name = strdup((const char *)text);
      xmlFree(text);
      LOG_INFO("🔖 Invocation name: %s", inv->name);
    }
  }
  else
  {
    inv->name = strdup(filename);
    LOG_WARN("⚠️ No name found for %s, using filename fallback", filename);
  }

  // === 2. Find <SourceList>
  xmlNodePtr source_list = NULL;
  for (xmlNodePtr child = tree->children; child; child = child->next)
  {
    if (child->type == XML_ELEMENT_NODE && xmlStrcmp(child->name, (const xmlChar *)"SourceList") == 0)
    {
      source_list = child;
      break;
    }
  }
  if (source_list)
  {
    parse_sources(source_list, (void **)&inv->sources, false);
  }
  else
  {
    LOG_WARN("⚠️ No SourceList found for Invocation %s", inv->name);
  }

  // === 3. Find <DestinationList>
  xmlNodePtr dest_list = NULL;
  for (xmlNodePtr child = tree->children; child; child = child->next)
  {
    if (child->type == XML_ELEMENT_NODE && xmlStrcmp(child->name, (const xmlChar *)"DestinationList") == 0)
    {
      dest_list = child;
      break;
    }
  }
  if (dest_list)
  {
    parse_destinations(dest_list, (void **)&inv->destinations, false); // false = Signal type
  }
  else
  {
    LOG_WARN("⚠️ No DestinationList found for Invocation %s", inv->name);
  }

  return inv;
}

void parse_block_from_xml(Block *blk, const char *inv_dir)
{
  DIR *dir = opendir(inv_dir);
  if (!dir)
  {
    LOG_ERROR("❌ Unable to open invocation directory: %s\n", inv_dir);
    exit(1);
  }

  LOG_INFO("🔍 Parsing block contents from directory: %s", inv_dir);

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL)
  {
    if (!strstr(entry->d_name, ".xml"))
      continue;

    char path[256];
    snprintf(path, sizeof(path), "%s/%s", inv_dir, entry->d_name);

    LOG_INFO("📄 Loading XML: %s", path);
    char *xml = load_file(path);
    if (!xml)
    {
      LOG_ERROR("❌ Failed to read file: %s", path);
      exit(1);
    }

    xmlDocPtr doc = xmlReadMemory(xml, strlen(xml), path, NULL, 0);
    free(xml);

    if (!doc)
    {
      LOG_ERROR("❌ Failed to parse XML tree: %s", path);
      exit(1);
    }

    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root)
    {
      LOG_ERROR("❌ Empty XML document: %s", path);
      xmlFreeDoc(doc);
      exit(1);
    }

    const char *top_tag = (const char *)root->name;
    const char *file_basename = entry->d_name;

    if (strcmp(top_tag, "Definition") == 0)
    {
      Definition *def = parse_definition(root, file_basename);
      if (!def)
      {
        LOG_ERROR("❌ Failed to parse Definition from: %s", path);
        xmlFreeDoc(doc);
        exit(1);
      }
      def->next = blk->definitions;
      blk->definitions = def;
      LOG_INFO("📦 Added Definition: %s", def->name);
    }
    else if (strcmp(top_tag, "Invocation") == 0)
    {
      Invocation *inv = parse_invocation(root, file_basename);
      if (!inv)
      {
        LOG_ERROR("❌ Failed to parse Invocation from: %s", path);
        xmlFreeDoc(doc);
        exit(1);
      }
      inv->next = blk->invocations;
      blk->invocations = inv;
      LOG_INFO("📦 Added Invocation: %s", inv->name);
    }
    else
    {
      LOG_ERROR("❌ Unknown top-level element: <%s> in file %s", top_tag, entry->d_name);
      xmlFreeDoc(doc);
      exit(1);
    }

    // Optional: move this block outside loop if you only want to link *after* all are parsed
    for (Invocation *inv = blk->invocations; inv; inv = inv->next)
    {
      char def_path[256];
      snprintf(def_path, sizeof(def_path), "%s/%s.xml", inv_dir, inv->name);
      inv->definition = parse_definition_from_file(def_path);

      if (!inv->definition)
      {
        LOG_ERROR("❌ Definition missing for Invocation: %s", inv->name);
        xmlFreeDoc(doc);
        exit(1);
      }

      LOG_INFO("🔗 Linked Invocation %s → Definition %s", inv->name, inv->definition->name);
    }

    xmlFreeDoc(doc);
  }

  closedir(dir);
  LOG_INFO("✅ Block population from XML completed.");
}

// Memory freeing functions
void free_signal(Signal *sig)
{
  while (sig)
  {
    Signal *next = sig->next;
    if (sig->name)
      free(sig->name);
    if (sig->content)
      free(sig->content);
    free(sig);
    sig = next;
  }
}

void free_source_places(SourcePlace *src)
{
  while (src)
  {
    SourcePlace *next = src->next;

    if (src->name)
      free(src->name);

    // Only free the signal if it's not the static NULL_SIGNAL sentinel
    if (src->signal && src->signal != &NULL_SIGNAL)
      free_signal(src->signal);

    free(src);
    src = next;
  }
}

void free_destination_places(DestinationPlace *dst)
{
  while (dst)
  {
    DestinationPlace *next = dst->next;
    if (dst->name)
      free(dst->name);
    if (dst->signal && dst->signal != &NULL_SIGNAL)
      free_signal(dst->signal);
    free(dst);
    dst = next;
  }
}

static void free_conditional_invocation(ConditionalInvocation *ci)
{
  if (!ci)
    return;
  if (ci->invocation_template)
    free(ci->invocation_template);

  ConditionalInvocationCase *c = ci->cases;
  while (c)
  {
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

static void free_definitions(Definition *def)
{
  while (def)
  {
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

static void free_invocations(Invocation *inv)
{
  while (inv)
  {
    Invocation *next = inv->next;
    if (inv->name)
      free(inv->name);
    free_source_places(inv->sources);
    free_destination_places(inv->destinations);
    free(inv);
    inv = next;
  }
}

void free_block(Block *blk)
{
  if (!blk)
    return;
  if (blk->psi)
    free(blk->psi);
  free_definitions(blk->definitions);
  free_invocations(blk->invocations);
  free(blk);
}
// Return 0 if all valid
int validate_snippets(const char *inv_dir)
{
  int invalid = 0;

  DIR *dir = opendir(inv_dir);
  if (!dir)
  {
    LOG_ERROR("❌ Cannot open invocation directory: %s", inv_dir);
    return -1;
  }

  LOG_INFO("🔍 Validating XML snippets in directory: %s", inv_dir);

  struct dirent *entry;
  int errors = 0;
  while ((entry = readdir(dir)) != NULL)
  {
    if (strstr(entry->d_name, ".xml"))
    {
      char path[256];
      snprintf(path, sizeof(path), "%s/%s", inv_dir, entry->d_name);

      LOG_INFO("📄 Validating file: %s", path);
      char *xml = load_file(path);
      if (!xml)
      {
        invalid = 1;
        LOG_WARN("⚠️ Skipping unreadable file: %s", path);
        continue;
      }

      // 🧠 Load XML document
      xmlDocPtr doc = xmlReadMemory(xml, strlen(xml), path, NULL, 0);
      free(xml);

      if (!doc)
      {
        LOG_ERROR("❌ Failed to parse XML: %s", path);
        invalid = 1;
        errors++;
        continue;
      }

      xmlNodePtr root = xmlDocGetRootElement(doc);
      if (!root)
      {
        LOG_ERROR("❌ Empty document or parse failure: %s", path);
        invalid = 1;
        errors++;
        xmlFreeDoc(doc);
        continue;
      }

      const char *tag = (const char *)root->name;
      if (strcmp(tag, "Invocation") == 0)
      {
        Invocation *inv = parse_invocation(root, entry->d_name);
        if (!inv)
        {
          invalid = 1;
          LOG_ERROR("❌ Invalid Invocation: %s", entry->d_name);
          errors++;
        }
        else
        {
          LOG_INFO("✅ Invocation parsed: %s", inv->name);
          free_invocations(inv);
        }
      }
      else if (strcmp(tag, "Definition") == 0)
      {
        Definition *def = parse_definition(root, entry->d_name);
        if (!def)
        {
          invalid = 1;
          LOG_ERROR("❌ Invalid Definition: %s", entry->d_name);
          errors++;
        }
        else
        {
          LOG_INFO("✅ Definition parsed: %s", def->name);
          free_definitions(def);
        }
      }
      else
      {
        invalid = 1;
        LOG_WARN("⚠️ Unknown top-level element in file: %s", path);
      }

      xmlFreeDoc(doc);
    }
  }

  closedir(dir);

  if (errors == 0)
  {
    LOG_INFO("✅ All snippets validated successfully.");
  }
  else
  {
    invalid = 1;
    LOG_WARN("⚠️ Validation finished with %d error(s).", errors);
  }

  return invalid;
}
