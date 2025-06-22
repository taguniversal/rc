#include "sexpr_parser.h"
#include "string_list.h"
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

SExpr *get_child_by_tag(const SExpr *parent, const char *tag)
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

int parse_block_from_sexpr(Block *blk, const char *inv_dir)
{
  DIR *dir = opendir(inv_dir);
  if (!dir)
  {
    LOG_ERROR("❌ Unable to open invocation directory: %s", inv_dir);
    exit(1);
  }

  LOG_INFO("🔍 parse_block_from_sexpr Parsing block contents from directory: %s", inv_dir);

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
      def->origin_sexpr_path = strdup(path);
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
      inv->origin_sexpr_path = strdup(path);
      inv->next = blk->invocations;
      blk->invocations = inv;
      LOG_INFO("📦 Added Invocation targeting: %s", inv->target_name);
    }
    else
    {
      LOG_ERROR("❌ Unknown top-level tag: (%s) in file %s", top_tag, file_basename);
      free_sexpr(expr);
      exit(1);
    }

    free_sexpr(expr);
  }

  closedir(dir);
  LOG_INFO("✅ Block population from S-expression completed.");

  return 0;
}

ConditionalInvocation *parse_conditional_invocation(const SExpr *ci_expr)
{
  if (!ci_expr || ci_expr->type != S_EXPR_LIST || ci_expr->count < 1)
    return NULL;

  if (ci_expr->list[0]->type != S_EXPR_ATOM || strcmp(ci_expr->list[0]->atom, "ConditionalInvocation") != 0)
    return NULL;

  ConditionalInvocation *ci = calloc(1, sizeof(ConditionalInvocation));
  ci->pattern_args = create_string_list(); // ✅ initialize flat StringList
  ci->arg_count = 0;
  ci->output = NULL;
  ci->cases = NULL;
  ci->case_count = 0;

  for (size_t i = 1; i < ci_expr->count; ++i)
  {
    const SExpr *item = ci_expr->list[i];
    if (!item || item->type != S_EXPR_LIST || item->count < 1)
      continue;

    const char *tag = item->list[0]->atom;

    // Parse Template
    if (strcmp(tag, "Template") == 0)
    {
      ci->arg_count = item->count - 1;

      for (size_t j = 1; j < item->count; ++j)
      {
        if (item->list[j]->type != S_EXPR_ATOM)
          continue;

        string_list_add(ci->pattern_args, item->list[j]->atom);
        LOG_INFO("🧩 Pattern arg[%zu]: %s", j - 1, item->list[j]->atom);
      }
    }

    // Parse Output
    else if (strcmp(tag, "Output") == 0 && item->count == 2)
    {
      if (item->list[1]->type == S_EXPR_ATOM)
      {
        ci->output = strdup(item->list[1]->atom);
        LOG_INFO("🔸 Output: %s", ci->output);
      }
    }

    // Parse Case
    else if (strcmp(tag, "Case") == 0 && item->count == 3)
    {
      const SExpr *key = item->list[1];
      const SExpr *val = item->list[2];
      if (key->type == S_EXPR_ATOM && val->type == S_EXPR_ATOM)
      {
        ci->cases = realloc(ci->cases, sizeof(ConditionalCase) * (ci->case_count + 1));
        ci->cases[ci->case_count].pattern = strdup(key->atom);
        ci->cases[ci->case_count].result = strdup(val->atom);
        LOG_INFO("📘 Case added: %s → %s", key->atom, val->atom);
        ci->case_count++;
      }
    }
  }

  return ci;
}

Definition *parse_definition(const SExpr *expr)
{
  if (!expr || expr->type != S_EXPR_LIST || expr->count < 1)
    return NULL;

  if (expr->list[0]->type != S_EXPR_ATOM || strcmp(expr->list[0]->atom, "Definition") != 0)
    return NULL;

  Definition *def = calloc(1, sizeof(Definition));
  def->input_signals = create_string_list();
  def->output_signals = create_string_list();

  for (size_t i = 1; i < expr->count; ++i)
  {
    const SExpr *item = expr->list[i];
    if (!item || item->type != S_EXPR_LIST || item->count < 1)
      continue;

    const char *tag = item->list[0]->atom;

    if (strcmp(tag, "Name") == 0 && item->count == 2)
    {
      def->name = strdup(item->list[1]->atom);
    }

    else if (strcmp(tag, "Inputs") == 0)
    {
      for (size_t j = 1; j < item->count; ++j)
      {
        const SExpr *signal = item->list[j];
        if (signal->type == S_EXPR_ATOM)
          string_list_add(def->input_signals, signal->atom);
      }
    }

    else if (strcmp(tag, "Outputs") == 0)
    {
      for (size_t j = 1; j < item->count; ++j)
      {
        const SExpr *signal = item->list[j];
        if (signal->type == S_EXPR_ATOM)
          string_list_add(def->output_signals, signal->atom);
      }
    }

    else if (strcmp(tag, "ConditionalInvocation") == 0)
    {
      def->sexpr_logic = sexpr_to_string(item);
      def->conditional_invocation = parse_conditional_invocation(item);
    }

    else if (strcmp(tag, "Body") == 0)
    {
      BodyItem *body_head = NULL;
      BodyItem **body_tail = &body_head;

      for (size_t j = 1; j < item->count; ++j)
      {
        const SExpr *sub = item->list[j];
        if (sub->type != S_EXPR_LIST || sub->count == 0)
          continue;

        if (sub->list[0]->type != S_EXPR_ATOM)
          continue;

        const char *sub_tag = sub->list[0]->atom;

        if (strcmp(sub_tag, "ConditionalInvocation") == 0)
        {
          def->sexpr_logic = sexpr_to_string(sub);
          def->conditional_invocation = parse_conditional_invocation(sub);
          continue; // still support having conditional logic inside Body
        }

        if (strcmp(sub_tag, "Invocation") == 0)
        {
          Invocation *inv = parse_invocation(sub);
          if (!inv)
            continue;

          BodyItem *bi = calloc(1, sizeof(BodyItem));
          bi->type = BODY_INVOCATION;
          bi->data.invocation = inv;
          bi->next = NULL;

          *body_tail = bi;
          body_tail = &bi->next;
        }
      }

      def->body = body_head;
    }
  }

  if (!def->name)
  {
    LOG_ERROR("⚠️  Definition missing a Name");
    destroy_string_list(def->input_signals);
    destroy_string_list(def->output_signals);
    free(def);
    return NULL;
  }

  return def;
}

Invocation *parse_invocation(SExpr *expr)
{
  if (!expr || expr->type != S_EXPR_LIST || expr->count == 0)
    return NULL;

  if (expr->list[0]->type != S_EXPR_ATOM || strcmp(expr->list[0]->atom, "Invocation") != 0)
    return NULL;

  Invocation *inv = calloc(1, sizeof(Invocation));
  inv->input_signals = create_string_list();
  inv->output_signals = create_string_list();
  inv->literal_bindings = calloc(1, sizeof(LiteralBindingList));

  // ─── First Pass: Parse structure (Target, Inputs, Outputs) ────────────────
  for (size_t i = 1; i < expr->count; ++i)
  {
    SExpr *form = expr->list[i];
    if (!form || form->type != S_EXPR_LIST || form->count == 0)
      continue;

    const char *tag = form->list[0]->atom;

    if (strcmp(tag, "Target") == 0 && form->count >= 2)
    {
      inv->target_name = strdup(form->list[1]->atom);
    }
    else if (strcmp(tag, "Inputs") == 0)
    {
      for (size_t j = 1; j < form->count; ++j)
      {
        SExpr *arg = form->list[j];
        if (arg->type == S_EXPR_ATOM)
          string_list_add(inv->input_signals, arg->atom);
      }
    }
    else if (strcmp(tag, "Outputs") == 0)
    {
      for (size_t j = 1; j < form->count; ++j)
      {
        SExpr *arg = form->list[j];
        if (arg->type == S_EXPR_ATOM)
          string_list_add(inv->output_signals, arg->atom);
      }
    }
  }

  // ─── Second Pass: Bindings ────────────────────────────────────────────────
  for (size_t i = 1; i < expr->count; ++i)
  {
    SExpr *form = expr->list[i];
    if (!form || form->type != S_EXPR_LIST || form->count == 0)
      continue;

    const char *tag = form->list[0]->atom;

    if (strcmp(tag, "bind") == 0 && form->count == 2)
    {
      SExpr *pair = form->list[1];
      if (pair->type == S_EXPR_LIST && pair->count == 2 &&
          pair->list[0]->type == S_EXPR_ATOM &&
          pair->list[1]->type == S_EXPR_ATOM)
      {
        const char *signal = pair->list[0]->atom;
        const char *value = pair->list[1]->atom;

        if (string_list_contains(inv->input_signals, signal))
        {
          LOG_INFO("📥 Literal binding recognized: %s = %s", signal, value);

          size_t i = inv->literal_bindings->count;
          inv->literal_bindings->items = realloc(
              inv->literal_bindings->items,
              sizeof(LiteralBinding) * (i + 1));
          inv->literal_bindings->items[i].name = strdup(signal);
          inv->literal_bindings->items[i].value = strdup(value);
          inv->literal_bindings->count++;
        }
        else
        {
          LOG_WARN("⚠️ Literal binding signal %s not in declared Inputs", signal);
        }
      }
    }
  }

  if (!inv->target_name)
  {
    LOG_ERROR("Invocation is missing a Target");
    destroy_string_list(inv->input_signals);
    destroy_string_list(inv->output_signals);
    free(inv);
    return NULL;
  }

  return inv;
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
