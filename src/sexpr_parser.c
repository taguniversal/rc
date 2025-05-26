#include "sexpr_parser.h"
#include "sexpr_parser_util.h"
#include "eval.h"
#include "log.h"
#include "rewrite_util.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

struct SExpr *parse_expr(const char **input);
static void emit_invocation(FILE *out, Invocation *inv, int indent);

static const char *skip_whitespace(const char *s)
{
  while (isspace(*s))
    s++;
  return s;
}

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

static char *parse_atom(const char **input)
{
  const char *start = *input;
  while (**input && !isspace(**input) && **input != '(' && **input != ')')
  {
    (*input)++;
  }
  size_t len = *input - start;
  char *atom = malloc(len + 1);
  strncpy(atom, start, len);
  atom[len] = '\0';
  return atom;
}

SExpr *get_child_by_tag(SExpr *parent, const char *tag)
{
  for (size_t i = 1; i < parent->count; ++i)
  {
    SExpr *child = parent->list[i];
    if (child->type == S_EXPR_LIST && child->count > 0)
    {
      SExpr *head = child->list[0];
      if (head->type == S_EXPR_ATOM && strcmp(head->atom, tag) == 0)
      {
        return child;
      }
    }
  }
  return NULL;
}

SExpr *get_child_by_value(SExpr *parent, const char *tag, const char *expected_value)
{
  if (!parent || parent->type != S_EXPR_LIST)
    return NULL;

  for (size_t i = 1; i < parent->count; ++i)
  {
    SExpr *child = parent->list[i];
    if (!child || child->type != S_EXPR_LIST || child->count < 2)
      continue;

    const char *child_tag = child->list[0]->atom;
    const char *child_val = child->list[1]->atom;

    if (child_tag && child_val && strcmp(child_tag, tag) == 0 && strcmp(child_val, expected_value) == 0)
      return child;
  }

  return NULL;
}

const char *get_atom_value(SExpr *list, size_t index)
{
  if (!list || index >= list->count)
    return NULL;
  SExpr *item = list->list[index];
  return (item->type == S_EXPR_ATOM) ? item->atom : NULL;
}

ConditionalInvocation *parse_conditional_invocation(SExpr *ci_expr)
{
  ConditionalInvocation *ci = calloc(1, sizeof(ConditionalInvocation));

  // 2. Parse Template
  SExpr *template_expr = get_child_by_tag(ci_expr, "Template");
  if (!template_expr || template_expr->count < 2)
  {
    LOG_ERROR("❌ ConditionalInvocation Missing Template");
    free(ci);
    return NULL;
  }

  ci->arg_count = template_expr->count - 1;
  ci->template_args = calloc(ci->arg_count, sizeof(char *));
  ci->resolved_template_args = calloc(ci->arg_count, sizeof(char *));

  size_t joined_len = 0;

  for (size_t i = 1; i < template_expr->count; ++i)
  {
    if (template_expr->list[i]->type != S_EXPR_ATOM)
    {
      LOG_WARN("⚠️ Template element %zu is not an atom — skipping", i);
      continue;
    }

    const char *arg = template_expr->list[i]->atom;
    ci->template_args[i - 1] = strdup(arg);
    ci->resolved_template_args[i - 1] = strdup(arg);

    LOG_INFO("🔣 Parsed template arg[%zu] = %s", i - 1, arg);

    joined_len += strlen(arg);
  }

  char *joined = calloc(joined_len + 1 + ci->arg_count, 1);
  for (size_t i = 0; i < ci->arg_count; ++i)
  {
    if (!ci->template_args[i])
    {
      LOG_ERROR("❌ Template arg[%zu] is NULL — skipping pattern creation", i);
      free(joined);
      return ci; // Let the failure show later
    }
    strcat(joined, "$");
    strcat(joined, ci->template_args[i]);
  }
  ci->invocation_template = joined;

  LOG_INFO("🧩 Template pattern joined: %s", joined);

  // 3. Parse Cases
  for (size_t i = 1; i < ci_expr->count; ++i)
  {
    SExpr *item = ci_expr->list[i];
    if (item->type != S_EXPR_LIST || item->count < 3)
      continue;

    SExpr *tag = item->list[0];
    if (tag->type != S_EXPR_ATOM || strcmp(tag->atom, "Case") != 0)
      continue;

    SExpr *val_attr = item->list[1];
    SExpr *result_atom = item->list[2];
    if (val_attr->type != S_EXPR_ATOM || result_atom->type != S_EXPR_ATOM)
    {
      LOG_ERROR("❌ ConditionalInvocation Malformed Case — expected atom pattern and result");
      exit(1);
    }

    ConditionalInvocationCase *cc = calloc(1, sizeof(ConditionalInvocationCase));
    cc->pattern = strdup(val_attr->atom);
    cc->result = strdup(result_atom->atom);
    cc->next = ci->cases;
    ci->cases = cc;

    LOG_INFO("📘 Added case: %s → %s", cc->pattern, cc->result);
  }

  return ci;
}

/*
The definition expresses the network of associations between the boundaries of the associated invocation.
A definition is a named syntax structure containing a source list, a destination list.
a place of resolution followed by a place of contained definitions. Definitiocnname is the correspondence name
of the definition. The source list is the input for the definition through which a formed name is received,
and the destination list is the output for the definition through which the results are delivered
The place of resolution is best understood as a bounded pure value expression that can contain association expressions.
*/
Definition *parse_definition(SExpr *expr)
{
  Definition *def = calloc(1, sizeof(Definition));

  if (!parse_definition_name(def, expr))
    return NULL;

  parse_definition_sources(def, expr);
  parse_definition_destinations(def, expr);
  char *pending_output = NULL; // 🔁 shared buffer for CI output inference
  parse_place_of_resolution(def, expr, &pending_output);
  parse_conditional_invocation_block(def, expr, &pending_output);
  parse_inline_invocations(def, expr);

  return def;
}

/*
The Invocation - The invocation associates destination places to form an input boundary and associates
source places to form an output boundary. The behavior model is that the boundaries are completeness
boundaries and that the invocation expresses completeness criterion behavior between its input and
output boundaries When the content at the output boundary is complete the content presented to the input
is complete. and the output is the correct resolution of the content presented to the input boundary.
Invocation boundaries are the boundaries of the expression. They are composition boundaries, coordination
boundaries, and partition boundaries.
*/
Invocation *parse_invocation(SExpr *expr)
{
  Invocation *inv = calloc(1, sizeof(Invocation));

  if (!parse_invocation_name(inv, expr))
  {
    free(inv);
    return NULL;
  }

  parse_invocation_destinations(inv, expr);
  parse_invocation_sources(inv, expr);

  return inv;
}
void validate_invocation_wiring(Block *blk)
{
  LOG_INFO("🧪 Validating invocation wiring...");

  for (Invocation *inv = blk->invocations; inv; inv = inv->next)
  {
    for (size_t i = 0; i < inv->sources.count; ++i)
    {
      SourcePlace *sp = inv->sources.items[i];
      const char *sig_name = sp->resolved_name ? sp->resolved_name : sp->name;
      if (!sp->content)
      {
        LOG_WARN("⚠️ Invocation '%s': SourcePlace '%s' is unwired or missing content",
                 inv->name, sig_name);
      }
      else
      {
        LOG_INFO("✅ Invocation '%s': SourcePlace '%s' has content [%s]",
                 inv->name, sig_name, sp->content);
      }
    }

    for (size_t i = 0; i < inv->destinations.count; ++i)
    {
      DestinationPlace *dp = inv->destinations.items[i];
      const char *sig_name = dp->resolved_name ? dp->resolved_name : dp->name;
      if (!dp->content)
      {
        LOG_WARN("⚠️ Invocation '%s': DestinationPlace '%s' is unwired or missing content",
                 inv->name, sig_name);
      }
      else
      {
        LOG_INFO("✅ Invocation '%s': DestinationPlace '%s' has content [%s]",
                 inv->name, sig_name, dp->content);
      }
    }
  }
}

void wire_por_sources_from_def_outputs(Definition *def)
{
  for (size_t i = 0; i < def->place_of_resolution_sources.count; ++i)
  {
    SourcePlace *por_src = def->place_of_resolution_sources.items[i];
    if (!por_src || !por_src->resolved_name)
      continue;

    for (size_t j = 0; j < def->destinations.count; ++j)
    {
      DestinationPlace *def_dst = def->destinations.items[j];
      if (!def_dst || !def_dst->resolved_name)
        continue;

      if (strcmp(por_src->resolved_name, def_dst->resolved_name) == 0)
      {
        if (def_dst->content)
        {
          if (por_src->content)
            free(por_src->content);
          por_src->content = strdup(def_dst->content);
          LOG_INFO("🔁 Wired POR Source '%s' ← Definition output [%s]",
                   por_src->resolved_name, def_dst->content);
        }
      }
    }
  }
}

int rewrite_signals(Block *blk)
{
  LOG_INFO("🧪 Starting global signal name rewriting pass...");

  LOG_INFO("🔁 Rewriting top-level invocations...");
  rewrite_top_level_invocations(blk);

  for (Definition *def = blk->definitions; def != NULL; def = def->next)
  {
    LOG_INFO("📘 Processing Definition: %s", def->name);

    LOG_INFO("  🔤 Rewriting definition-level signal names...");
    rewrite_definition_signals(def);

    LOG_INFO("  🧠 Rewriting PlaceOfResolution invocations...");
    for (size_t i = 0; i < def->place_of_resolution_sources.count; ++i)
    {
      SourcePlace *sp = def->place_of_resolution_sources.items[i];
      LOG_INFO("🔎 Pre-rewrite POR SourcePlace: name=%s", sp && sp->name ? sp->name : "NULL");
    }

    rewrite_por_invocations(def);

    wire_por_sources_from_def_outputs(def);

    LOG_INFO("🔧 Patch resolved_template_args for CI to use fully qualified signal names");
    rewrite_conditional_invocation(def);

    LOG_INFO("  🔌 Wiring outputs → POR sources...");
    //  wire_por_outputs_to_sources(def);

    LOG_INFO("  🔌 Wiring POR sources → outputs...");
    // wire_por_sources_to_outputs(def);
  }

  LOG_INFO("🧹 Cleaning up instance counters...");
  cleanup_name_counters();

  LOG_INFO("✅ Signal rewrite pass completed.");
  return 0;
}

int parse_block_from_sexpr(Block *blk, const char *inv_dir)
{
  DIR *dir = opendir(inv_dir);
  if (!dir)
  {
    LOG_ERROR("❌ Unable to open invocation directory: %s", inv_dir);
    exit(1);
  }

  LOG_INFO(
      "🔍 parse_block_from_sexpr Parsing block contents from directory: %s",
      inv_dir);

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL)
  {
    if (!strstr(entry->d_name, ".sexpr"))
      continue;

    char path[256];
    snprintf(path, sizeof(path), "%s/%s", inv_dir, entry->d_name);

    LOG_INFO("📄 Loading S-expression: %s", path);
    char *contents = load_file(path);
    if (!contents)
    {
      LOG_ERROR("❌ Failed to read file: %s", path);
      exit(1);
    }

    SExpr *expr = parse_sexpr(contents);
    free(contents);

    if (!expr || expr->type != S_EXPR_LIST || expr->count == 0)
    {
      LOG_ERROR("❌ Invalid or empty S-expression in file: %s", path);
      if (expr)
        free_sexpr(expr);
      exit(1);
    }

    SExpr *head = expr->list[0];
    if (head->type != S_EXPR_ATOM)
    {
      LOG_ERROR("❌ Malformed S-expression (non-atom head) in file: %s", path);
      free_sexpr(expr);
      exit(1);
    }

    const char *top_tag = head->atom;
    const char *file_basename = entry->d_name;

    if (strcmp(top_tag, "Definition") == 0)
    {
      Definition *def = parse_definition(expr);
      if (!def)
      {
        LOG_ERROR("❌ Failed to parse Definition from: %s", path);
        free_sexpr(expr);
        exit(1);
      }
      def->next = blk->definitions;
      blk->definitions = def;
      LOG_INFO("📦 Added Definition: %s", def->name);
    }
    else if (strcmp(top_tag, "Invocation") == 0)
    {
      Invocation *inv = parse_invocation(expr);
      if (!inv)
      {
        LOG_ERROR("❌ Failed to parse Invocation from: %s", path);
        free_sexpr(expr);
        exit(1);
      }
      inv->next = blk->invocations;
      blk->invocations = inv;
      LOG_INFO("📦 Added Invocation: %s", inv->name);
    }
    else
    {
      LOG_ERROR("❌ Unknown top-level tag: (%s) in file %s", top_tag,
                file_basename);
      free_sexpr(expr);
      exit(1);
    }

    free_sexpr(expr);
  }

  closedir(dir);

  LOG_INFO("✅ Block population from S-expression completed.");

  return 0;
}

static struct SExpr *parse_list(const char **input)
{
  struct SExpr *expr = malloc(sizeof(struct SExpr));
  expr->type = S_EXPR_LIST;
  expr->list = NULL;
  expr->count = 0;

  *input = skip_whitespace(*input);
  while (**input && **input != ')')
  {
    struct SExpr *child = parse_expr(input);
    expr->list =
        realloc(expr->list, sizeof(struct SExpr *) * (expr->count + 1));
    expr->list[expr->count++] = child;
    *input = skip_whitespace(*input);
  }

  if (**input == ')')
    (*input)++;
  return expr;
}

SExpr *parse_expr(const char **input)
{
  *input = skip_whitespace(*input);
  if (**input == '(')
  {
    (*input)++;
    return parse_list(input);
  }
  else
  {
    struct SExpr *atom = malloc(sizeof(struct SExpr));
    atom->type = S_EXPR_ATOM;
    atom->atom = parse_atom(input);
    atom->list = NULL;
    atom->count = 0;
    return atom;
  }
}

SExpr *parse_sexpr(const char *input) { return parse_expr(&input); }

void print_sexpr(const SExpr *expr, int indent)
{
  if (expr->type == S_EXPR_ATOM)
  {
    LOG_INFO("%*s%s", indent, "", expr->atom);
  }
  else
  {
    LOG_INFO("%*s(", indent, "");
    for (size_t i = 0; i < expr->count; ++i)
    {
      print_sexpr(expr->list[i], indent + 2);
    }
    LOG_INFO("%*s)", indent, "");
  }
}

void free_sexpr(SExpr *expr)
{
  if (expr->type == S_EXPR_LIST)
  {
    for (size_t i = 0; i < expr->count; ++i)
    {
      free_sexpr(expr->list[i]);
    }
    free(expr->list);
  }
  else if (expr->type == S_EXPR_ATOM)
  {
    free(expr->atom);
  }
  free(expr);
}

static void emit_indent(FILE *out, int indent)
{
  for (int i = 0; i < indent; ++i)
    fputc(' ', out);
}

static void emit_atom(FILE *out, const char *atom)
{
  // Quote if needed (e.g., contains non-alphanum)
  if (strpbrk(atom, " ()\"\t\n"))
    fprintf(out, "\"%s\"", atom);
  else
    fputs(atom, out);
}
static void emit_source_list(FILE *out, SourcePlaceList list, int indent, const char *role_hint)
{
  emit_indent(out, indent);
  fputs("(SourceList\n", out);

  for (size_t i = 0; i < list.count; ++i)
  {
    SourcePlace *src = list.items[i];
    emit_indent(out, indent + 2);
    fputs("(SourcePlace\n", out);

    const char *signame = src->resolved_name ? src->resolved_name : src->name;
    if (signame)
    {
      emit_indent(out, indent + 4);
      fputs("(Name ", out);
      emit_atom(out, signame);
      fputs(")\n", out);

      emit_indent(out, indent + 4);
      fprintf(out, ";; role: %s\n", role_hint ? role_hint : "unknown");
    }

    if (src->content)
    {
      LOG_INFO(
          "✏️ Emitting SourcePlace: name=%s signal=%s",
          signame,
          src->content);

      emit_indent(out, indent + 4);
      fputs("(Signal\n", out);
      emit_indent(out, indent + 6);
      fputs("(Content ", out);
      emit_atom(out, src->content);
      fputs(")\n", out);
      emit_indent(out, indent + 4);
      fputs(")\n", out);
    }
    else
    {
      emit_indent(out, indent + 4);
      fputs(";; ⚠️ Unwired SourcePlace\n", out);
    }

    emit_indent(out, indent + 2);
    fputs(")\n", out);
  }

  emit_indent(out, indent);
  fputs(")\n", out);
}
static void emit_destination_list(FILE *out, DestinationPlaceList list,
                                  int indent, const char *role_hint)
{
  emit_indent(out, indent);
  fputs("(DestinationList\n", out);

  for (size_t i = 0; i < list.count; ++i)
  {
    DestinationPlace *dst = list.items[i];

    emit_indent(out, indent + 2);
    fputs("(DestinationPlace\n", out);

    const char *signame = dst->resolved_name ? dst->resolved_name : dst->name;
    if (signame)
    {
      emit_indent(out, indent + 4);
      fputs("(Name ", out);
      emit_atom(out, signame);
      fputs(")\n", out);

      // 🔍 Add per-place role annotation
      emit_indent(out, indent + 4);
      fprintf(out, ";; role: %s\n", role_hint ? role_hint : "unknown");
    }

    if (dst->content)
    {
      emit_indent(out, indent + 4);
      fputs("(Signal\n", out);
      emit_indent(out, indent + 6);
      fputs("(Content ", out);
      emit_atom(out, dst->content);
      fputs(")\n", out);
      emit_indent(out, indent + 4);
      fputs(")\n", out);
    }

    emit_indent(out, indent + 2);
    fputs(")\n", out);
  }

  emit_indent(out, indent);
  fputs(")\n", out);
}

void emit_place_of_resolution(FILE *out, Definition *def, int indent)
{
  if (!def || (!def->place_of_resolution_invocations && def->place_of_resolution_sources.count == 0))
    return;

  emit_indent(out, indent);
  fputs("(PlaceOfResolution\n", out);

  // Emit internal invocations
  for (Invocation *inv = def->place_of_resolution_invocations; inv; inv = inv->next)
  {
    emit_invocation(out, inv, indent + 2);
  }

  // Emit resolved output sources from array
  for (size_t i = 0; i < def->place_of_resolution_sources.count; ++i)
  {
    SourcePlace *sp = def->place_of_resolution_sources.items[i];
    if (!sp)
      continue;

    emit_indent(out, indent + 2);
    fputs("(SourcePlace\n", out);

    if (sp->resolved_name)
    {
      emit_indent(out, indent + 4);
      fprintf(out, "(Name %s)\n", sp->resolved_name);
    }

    emit_indent(out, indent + 4);
    fputs("(ContentFrom ConditionalInvocationResult)\n", out); // placeholder

    emit_indent(out, indent + 2);
    fputs(")\n", out);
  }

  emit_indent(out, indent);
  fputs(")\n", out);
}

static void emit_invocation(FILE *out, Invocation *inv, int indent)
{
  emit_indent(out, indent);
  fputs("(Invocation\n", out);

  emit_indent(out, indent + 2);
  fputs("(Name ", out);
  emit_atom(out, inv->name);
  fputs(")\n", out);

  // 🚧 Role Hint: Inputs for invocation
  emit_indent(out, indent + 2);
  fputs(";; Inputs (flow into definition)\n", out);
  emit_destination_list(out, inv->destinations, indent + 2, "input");

  // 🚧 Role Hint: Outputs from invocation
  emit_indent(out, indent + 2);
  fputs(";; Outputs (flow out of definition)\n", out);
  emit_source_list(out, inv->sources, indent + 2, "output");

  emit_indent(out, indent);
  fputs(")\n", out);
}

static void emit_conditional(FILE *out, ConditionalInvocation *ci, int indent)
{
  emit_indent(out, indent);
  fputs("(ConditionalInvocation\n", out);

  // Emit Resolved Template  emit_indent(out, indent + 2);
  fputs("(Template", out);
  for (size_t i = 0; i < ci->arg_count; ++i)
  {
    const char *arg = ci->resolved_template_args[i];
    if (arg)
    {
      fprintf(out, " %s", arg);
    }
    else
    {
      fprintf(out, " ???"); // fallback for debugging
    }
  }
  fputs(")\n", out);

  // Emit Cases
  for (ConditionalInvocationCase *c = ci->cases; c; c = c->next)
  {
    emit_indent(out, indent + 2);
    fprintf(out, "(Case %s %s)\n", c->pattern, c->result);
  }

  emit_indent(out, indent);
  fputs(")\n", out);
}

void emit_definition(FILE *out, Definition *def, int indent)
{
  LOG_INFO("🧪 Emitting definition: %s", def->name);

  emit_indent(out, indent);
  fputs("(Definition\n", out);

  // Emit definition name
  emit_indent(out, indent + 2);
  fputs("(Name ", out);
  emit_atom(out, def->name);
  fputs(")\n", out);

  // 🚧 Role Hint: Inputs to the definition
  emit_indent(out, indent + 2);
  fputs(";; Inputs (from invocation → used inside definition %s\n", out);
  emit_source_list(out, def->sources, indent + 2, "input");

  // 🚧 Role Hint: Outputs from the definition
  emit_indent(out, indent + 2);
  fputs(";; Outputs (from definition → back to invocation)\n", out);
  emit_destination_list(out, def->destinations, indent + 2, "output");

  // Emit PlaceOfResolution if present
  LOG_INFO("🧪 Emitting POR for def: %s", def->name);
  if (!def->place_of_resolution_invocations && def->place_of_resolution_sources.count == 0)
  {
    LOG_WARN("⚠️ No POR data found in def: %s", def->name);
  }
  else
  {
    LOG_INFO("✅ POR present for def: %s", def->name);
  }

  emit_place_of_resolution(out, def, indent + 2);

  // Emit ConditionalInvocation if present
  if (def->conditional_invocation)
  {
    emit_conditional(out, def->conditional_invocation, indent + 2);
  }

  // Emit nested invocations if present
  for (Invocation *inv = def->invocations; inv; inv = inv->next)
  {
    emit_invocation(out, inv, indent + 2);
  }

  emit_indent(out, indent);
  fputs(")\n", out);
}

void emit_all_definitions(Block *blk, const char *dirpath)
{
  // Create the output directory if it doesn't exist
  if (mkdir(dirpath, 0755) != 0 && errno != EEXIST)
  {
    LOG_ERROR("❌ Failed to create output directory: %s", dirpath);
    return;
  }

  for (Definition *def = blk->definitions; def; def = def->next)
  {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.def.sexpr", dirpath, def->name);
    FILE *out = fopen(path, "w");
    if (!out)
    {
      LOG_ERROR("❌ Failed to open file for writing: %s", path);
      continue;
    }
    // TODO DEBUG
    for (Definition *def = blk->definitions; def; def = def->next)
    {
      LOG_INFO("👀 Definition in blk: %s", def->name);
      if (def->place_of_resolution_invocations)
      {
        LOG_INFO("🔍 Has POR invocations");
      }
    }
    emit_definition(out, def, 0);
    fclose(out);
    LOG_INFO("✅ Wrote definition '%s' to %s", def->name, path);
  }
}

void emit_all_invocations(Block *blk, const char *dirpath)
{
  // Create the output directory if it doesn't exist
  if (mkdir(dirpath, 0755) != 0 && errno != EEXIST)
  {
    LOG_ERROR("❌ Failed to create output directory: %s", dirpath);
    return;
  }

  for (Invocation *inv = blk->invocations; inv; inv = inv->next)
  {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.inv.sexpr", dirpath, inv->name);
    FILE *out = fopen(path, "w");
    if (!out)
    {
      LOG_ERROR("❌ Failed to open file for writing: %s", path);
      continue;
    }

    // Emit the invocation
    emit_invocation(out, inv, 0);
    fclose(out);
    LOG_INFO("✅ Wrote invocation '%s' to %s", inv->name, path);
  }
}
