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

ConditionalInvocation *parse_conditional_invocation(const SExpr *ci_expr)
{
    if (!ci_expr || ci_expr->type != S_EXPR_LIST || ci_expr->count < 1)
        return NULL;

    if (ci_expr->list[0]->type != S_EXPR_ATOM || strcmp(ci_expr->list[0]->atom, "ConditionalInvocation") != 0)
        return NULL;

    ConditionalInvocation *ci = calloc(1, sizeof(ConditionalInvocation));
    ci->pattern = NULL;
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
            size_t total_len = 0;
            for (size_t j = 1; j < item->count; ++j)
                total_len += strlen(item->list[j]->atom);

            char *pattern = calloc(total_len + 1, 1);
            for (size_t j = 1; j < item->count; ++j)
                strcat(pattern, item->list[j]->atom);

            ci->pattern = pattern;
            LOG_INFO("🧩 Pattern: %s", pattern);
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
                ci->case_count++;
                LOG_INFO("📘 Case added: %s → %s", key->atom, val->atom);
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

    for (size_t i = 1; i < expr->count; ++i) {
        const SExpr *item = expr->list[i];
        if (!item || item->type != S_EXPR_LIST || item->count < 1)
            continue;

        const char *tag = item->list[0]->atom;

        if (strcmp(tag, "Name") == 0 && item->count == 2) {
            def->name = strdup(item->list[1]->atom);
        }

        else if (strcmp(tag, "Inputs") == 0) {
            for (size_t j = 1; j < item->count; ++j) {
                const SExpr *signal = item->list[j];
                if (signal->type == S_EXPR_ATOM)
                    string_list_add(def->input_signals, signal->atom);
            }
        }

        else if (strcmp(tag, "Outputs") == 0) {
            for (size_t j = 1; j < item->count; ++j) {
                const SExpr *signal = item->list[j];
                if (signal->type == S_EXPR_ATOM)
                    string_list_add(def->output_signals, signal->atom);
            }
        }

        else if (strcmp(tag, "ConditionalInvocation") == 0) {
            def->sexpr_logic = sexpr_to_string(item);
            def->conditional_invocation = parse_conditional_invocation(item);
        }

        else if (strcmp(tag, "Body") == 0) {
            for (size_t j = 1; j < item->count; ++j) {
                const SExpr *sub = item->list[j];
                if (sub->type == S_EXPR_LIST && sub->count > 0 &&
                    sub->list[0]->type == S_EXPR_ATOM &&
                    strcmp(sub->list[0]->atom, "ConditionalInvocation") == 0)
                {
                    def->sexpr_logic = sexpr_to_string(sub);
                    def->conditional_invocation = parse_conditional_invocation(sub);
                    break;  // support only one ConditionalInvocation per Body
                }
            }
        }
    }

    if (!def->name) {
        LOG_ERROR("⚠️  Definition missing a Name");
        destroy_string_list(def->input_signals);
        destroy_string_list(def->output_signals);
        free(def);
        return NULL;
    }

    return def;
}

Invocation *parse_invocation(SExpr *expr) {
  if (!expr || expr->type != S_EXPR_LIST || expr->count == 0)
      return NULL;

  if (expr->list[0]->type != S_EXPR_ATOM || strcmp(expr->list[0]->atom, "Invocation") != 0)
      return NULL;

  Invocation *inv = calloc(1, sizeof(Invocation));
  inv->input_signals = create_string_list();
  inv->output_signals = create_string_list();

  for (size_t i = 1; i < expr->count; ++i) {
      SExpr *form = expr->list[i];
      if (!form || form->type != S_EXPR_LIST || form->count == 0)
          continue;

      const char *tag = form->list[0]->atom;

      if (strcmp(tag, "Target") == 0 && form->count >= 2) {
          inv->target_name = strdup(form->list[1]->atom);
      }

      else if (strcmp(tag, "Inputs") == 0) {
          for (size_t j = 1; j < form->count; ++j) {
              SExpr *arg = form->list[j];
              if (arg->type == S_EXPR_LIST && arg->count >= 2 && strcmp(arg->list[0]->atom, "Literal") == 0) {
                  string_list_add(inv->input_signals, arg->list[1]->atom);
              } else if (arg->type == S_EXPR_ATOM) {
                  string_list_add(inv->input_signals, arg->atom);
              }
          }
      }

      else if (strcmp(tag, "Outputs") == 0) {
          for (size_t j = 1; j < form->count; ++j) {
              SExpr *arg = form->list[j];
              if (arg->type == S_EXPR_ATOM)
                  string_list_add(inv->output_signals, arg->atom);
          }
      }
  }

  if (!inv->target_name) {
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
