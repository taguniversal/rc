#include "sexpr_parser.h"
#include "eval.h"
#include "log.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static struct SExpr *parse_expr(const char **input);

typedef struct NameCounter
{
  const char *name;
  int count;
  struct NameCounter *next;
} NameCounter;

static NameCounter *counters = NULL;

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

  // 1. Parse Output
  SExpr *output_expr = get_child_by_tag(ci_expr, "Output");
  if (output_expr && output_expr->count > 1 &&
      output_expr->list[1]->type == S_EXPR_ATOM)
  {
    ci->output = strdup(output_expr->list[1]->atom);
  }
  else
  {
    LOG_WARN("⚠️ ConditionalInvocation missing (Output ...) field; defaulting to NULL");
    ci->output = NULL;
  }

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
      continue;

    const char *arg = template_expr->list[i]->atom;
    ci->template_args[i - 1] = strdup(arg);   // raw arg
    ci->resolved_template_args[i - 1] = NULL; // to be populated later
    joined_len += strlen(arg);
  }

  // Create joined template string: e.g., "$A$B" → "$A$B"
  char *joined = calloc(joined_len + 1 + ci->arg_count, 1); // add space for $ separators
  for (size_t i = 0; i < ci->arg_count; ++i)
  {
    strcat(joined, "$");
    strcat(joined, ci->template_args[i]);
  }
  ci->invocation_template = joined;

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
  }

  return ci;
}

int parse_signal(SExpr *sig_expr, Signal **out_signal)
{

  if (!sig_expr || !out_signal)
    return 0;

  // Unwrap the (Signal (...)) wrapper if needed
  if (sig_expr->count > 0 &&
      sig_expr->list[0]->type == S_EXPR_ATOM &&
      strcmp(sig_expr->list[0]->atom, "Signal") == 0)
  {
    // Try to find Content inside
    for (size_t i = 1; i < sig_expr->count; ++i)
    {
      SExpr *child = sig_expr->list[i];
      if (child->type != S_EXPR_LIST || child->count < 2)
        continue;
      if (child->list[0]->type == S_EXPR_ATOM &&
          strcmp(child->list[0]->atom, "Content") == 0 &&
          child->list[1]->type == S_EXPR_ATOM)
      {

        Signal *sig = calloc(1, sizeof(Signal));
        sig->content = strdup(child->list[1]->atom);
        sig->next = NULL;
        *out_signal = sig;

        LOG_INFO("💡 parse_signal: Parsed signal content [%s]", sig->content);
        return 1;
      }
    }

    LOG_WARN("⚠️ parse_signal: Signal block found, but no valid Content child");
    return 0;
  }

  // Handle bare (Content 1) in case that's ever directly passed
  if (sig_expr->count >= 2 &&
      sig_expr->list[0]->type == S_EXPR_ATOM &&
      strcmp(sig_expr->list[0]->atom, "Content") == 0 &&
      sig_expr->list[1]->type == S_EXPR_ATOM)
  {

    Signal *sig = calloc(1, sizeof(Signal));
    sig->content = strdup(sig_expr->list[1]->atom);
    sig->next = NULL;
    *out_signal = sig;

    LOG_INFO("💡 parse_signal: Parsed bare content [%s]", sig->content);
    return 1;
  }

  LOG_WARN("⚠️ parse_signal: Unexpected signal format");
  print_sexpr(sig_expr, 6);
  return 0;
}

Definition *parse_definition(SExpr *expr)
{
  Definition *def = calloc(1, sizeof(Definition));

  // 1. Get Name
  SExpr *name_expr = get_child_by_tag(expr, "Name");
  const char *name = get_atom_value(name_expr, 1);
  if (!name)
  {
    LOG_ERROR("❌ [Definition:%s] Missing or invalid Name");
    free(def);
    return NULL;
  }

  def->name = strdup(name);

  // 2. Get SourceList
  SExpr *src_list = get_child_by_tag(expr, "SourceList");
  if (src_list)
  {
    for (size_t i = 1; i < src_list->count; ++i)
    {
      SExpr *place = src_list->list[i];
      if (place->type != S_EXPR_LIST)
        continue;

      SourcePlace *sp = calloc(1, sizeof(SourcePlace));
      sp->signal = &NULL_SIGNAL;

      for (size_t j = 1; j < place->count; ++j)
      {
        SExpr *field = place->list[j];
        if (field->type != S_EXPR_LIST || field->count == 0)
          continue;

        const char *tag = get_atom_value(field, 0);
        if (!tag)
          continue;

        else if (strcmp(tag, "Signal") == 0)
        {
          for (size_t k = 1; k < field->count; ++k)
          {
            if (field->list[k]->type == S_EXPR_LIST)
            {
              LOG_INFO("🧪 About to call parse_signal with:");
              print_sexpr(field->list[k], 8);

              parse_signal(field->list[k], &sp->signal); // or dp->signal
            }
          }
        }

        else if (field->count >= 2)
        {
          const char *val = get_atom_value(field, 1);
          if (!val)
            continue;

          if (strcmp(tag, "Name") == 0)
            sp->name = strdup(val);
        }
      }

      sp->next = def->sources;
      def->sources = sp;

      LOG_INFO("🧩 Parsed SourcePlace in def %s: name=%s signal_content=%s",
               def->name, sp->name ? sp->name : "null",
               (sp->signal && sp->signal != &NULL_SIGNAL && sp->signal->content)
                   ? sp->signal->content
                   : "null");
    }
  }
  else
  {
    LOG_ERROR("❌ [Definition:%s] Missing SourceList", def->name);
  }

  // 3. Get DestinationList
  SExpr *dst_list = get_child_by_tag(expr, "DestinationList");
  if (dst_list)
  {
    for (size_t i = 1; i < dst_list->count; ++i)
    {
      SExpr *place = dst_list->list[i];
      if (place->type != S_EXPR_LIST)
        continue;

      DestinationPlace *dp = calloc(1, sizeof(DestinationPlace));
      dp->signal = &NULL_SIGNAL;

      for (size_t j = 1; j < place->count; ++j)
      {
        SExpr *field = place->list[j];
        if (field->type != S_EXPR_LIST || field->count == 0)
          continue;

        const char *tag = get_atom_value(field, 0);
        if (!tag)
          continue;

        else if (strcmp(tag, "Signal") == 0)
        {
          for (size_t k = 1; k < field->count; ++k)
          {
            if (field->list[k]->type == S_EXPR_LIST)
            {
              LOG_INFO("🧪 About to call parse_signal with:");
              print_sexpr(field->list[k], 8);

              parse_signal(field->list[k], &dp->signal);
            }
          }
        }

        else if (field->count >= 2)
        {
          const char *val = get_atom_value(field, 1);
          if (!val)
            continue;

          if (strcmp(tag, "Name") == 0)
            dp->name = strdup(val);
        }
      }

      dp->next = def->destinations;
      def->destinations = dp;

      LOG_INFO(
          "🧩 Parsed DestinationPlace in def %s: name=%s signal_content=%s",
          def->name, dp->name ? dp->name : "null",
          (dp->signal && dp->signal != &NULL_SIGNAL && dp->signal->content)
              ? dp->signal->content
              : "null");
    }
  }
  else
  {
    LOG_ERROR("❌ [Definition:%s] Missing DestinationList", def->name);
  }

  // 4. ConditionalInvocation
  SExpr *ci_expr = get_child_by_tag(expr, "ConditionalInvocation");
  if (ci_expr)
  {
    def->conditional_invocation = parse_conditional_invocation(ci_expr);
    if (def->conditional_invocation)
    {
      LOG_INFO("🧩 ConditionalInvocation parsed for %s", def->name);
      for (ConditionalInvocationCase *c = def->conditional_invocation->cases; c;
           c = c->next)
      {
        LOG_INFO("    Case %s → %s", c->pattern, c->result);
      }
    }
  }

  // 5. Inline Invocations
  for (size_t i = 1; i < expr->count; ++i)
  {
    SExpr *item = expr->list[i];
    if (item->type != S_EXPR_LIST || item->count < 1)
      continue;

    SExpr *head = item->list[0];
    if (head->type != S_EXPR_ATOM || strcmp(head->atom, "Invocation") != 0)
      continue;

    Invocation *inv = parse_invocation(item);
    if (!inv)
    {
      LOG_ERROR("❌ Failed to parse inline Invocation inside %s", def->name);
      continue;
    }

    inv->next = def->invocations;
    def->invocations = inv;

    LOG_INFO("🔹 Parsed inline Invocation: %s in %s", inv->name, def->name);
  }

  return def;
}

Invocation *parse_invocation(SExpr *expr)
{
  Invocation *inv = calloc(1, sizeof(Invocation));

  // ✅ Name
  SExpr *name_expr = get_child_by_tag(expr, "Name");
  const char *name = get_atom_value(name_expr, 1);
  if (!name)
  {
    LOG_ERROR("❌ [Invocation] Missing or invalid Name");
    free(inv);
    return NULL;
  }
  inv->name = strdup(name);

  // ✅ DestinationList
  SExpr *dst_list = get_child_by_tag(expr, "DestinationList");
  if (dst_list)
  {
    for (size_t i = 1; i < dst_list->count; ++i)
    {
      SExpr *place = dst_list->list[i];
      if (place->type != S_EXPR_LIST)
        continue;

      DestinationPlace *dp = calloc(1, sizeof(DestinationPlace));
      dp->signal = &NULL_SIGNAL;

      bool has_name = false;

      for (size_t j = 1; j < place->count; ++j)
      {
        SExpr *field = place->list[j];
        if (field->type != S_EXPR_LIST || field->count < 2)
          continue;

        const char *tag = get_atom_value(field, 0);
        if (!tag)
          continue;

        if (strcmp(tag, "Name") == 0)
        {
          const char *val = get_atom_value(field, 1);
          if (val)
          {
            dp->name = strdup(val);
            has_name = true;
          }
        }
        else if (strcmp(tag, "Signal") == 0)
        {
          for (size_t k = 1; k < field->count; ++k)
          {
            if (field->list[k]->type == S_EXPR_LIST)
            {
              LOG_INFO("🧪 About to call parse_signal with:");
              print_sexpr(field->list[k], 8);
              parse_signal(field->list[k], &dp->signal);
            }
          }
        }
      }

      // 🧠 If no name was provided, synthesize one
      if (!has_name)
      {
        char synth[64];
        snprintf(synth, sizeof(synth), "%s.0.in%zu", inv->name, i - 1);
        dp->name = strdup(synth);
        LOG_INFO("🧠 Synthesized destination name: %s", dp->name);
      }

      dp->next = inv->destinations;
      inv->destinations = dp;
    }
  }
  else
  {
    LOG_ERROR("❌ [Invocation:%s] Missing DestinationList", inv->name);
  }

  // ✅ SourceList
  SExpr *src_list = get_child_by_tag(expr, "SourceList");
  if (src_list)
  {
    for (size_t i = 1; i < src_list->count; ++i)
    {
      SExpr *place = src_list->list[i];
      if (place->type != S_EXPR_LIST)
        continue;

      SourcePlace *sp = calloc(1, sizeof(SourcePlace));
      sp->signal = &NULL_SIGNAL;

      for (size_t j = 1; j < place->count; ++j)
      {
        SExpr *field = place->list[j];
        if (field->type != S_EXPR_LIST || field->count < 2)
          continue;

        const char *tag = get_atom_value(field, 0);
        const char *val = get_atom_value(field, 1);
        if (!tag || !val)
          continue;

        if (strcmp(tag, "Name") == 0)
        {
          sp->name = strdup(val);
        }
        else if (strcmp(tag, "Signal") == 0)
        {
          for (size_t k = 1; k < field->count; ++k)
          {
            if (field->list[k]->type == S_EXPR_LIST)
            {
              LOG_INFO("🧪 About to call parse_signal with:");
              print_sexpr(field->list[k], 8);
              parse_signal(field->list[k], &sp->signal);
            }
          }
        }
      }

      sp->next = inv->sources;
      inv->sources = sp;

      LOG_INFO("🧩 Parsed SourcePlace: name=%s, content=%s",
               sp->name ? sp->name : "null",
               (sp->signal && sp->signal != &NULL_SIGNAL && sp->signal->content)
                   ? sp->signal->content
                   : "null");
    }
  }
  else
  {
    LOG_ERROR("❌ [Invocation:%s] Missing SourceList", inv->name);
  }

  return inv;
}
bool validate_sexpr(SExpr *expr, const char *filename)
{
  if (!expr || expr->type != S_EXPR_LIST || expr->count < 1)
    return false;

  const char *tag = expr->list[0]->atom;
  if (!tag)
    return false;

  bool is_invocation = strcmp(tag, "Invocation") == 0;
  bool is_definition = strcmp(tag, "Definition") == 0;

  if (!is_invocation && !is_definition)
    return true; // Skip validation for other types

  bool seen_dest = false;
  bool seen_source = false;

  for (size_t i = 1; i < expr->count; ++i)
  {
    SExpr *child = expr->list[i];
    if (!child || child->type != S_EXPR_LIST || child->count < 1)
      continue;

    const char *subtag = child->list[0]->atom;
    if (!subtag)
      continue;

    if (strcmp(subtag, "SourceList") == 0)
    {
      if (is_invocation && !seen_dest)
      {
        LOG_ERROR("❌ [%s] Invocation: SourceList must come after DestinationList", filename);
        return false;
      }
      if (is_definition)
      {
        if (seen_source)
        {
          LOG_ERROR("❌ [%s] Definition: Only one SourceList allowed", filename);
          return false;
        }
        if (seen_dest)
        {
          LOG_ERROR("❌ [%s] Definition: SourceList must come before DestinationList", filename);
          return false;
        }
      }

      seen_source = true;

      for (size_t j = 1; j < child->count; ++j)
      {
        SExpr *sp = child->list[j];
        if (!sp || sp->type != S_EXPR_LIST)
          continue;

        bool has_name = get_child_by_tag(sp, "Name") != NULL;
        if (!has_name)
        {
          LOG_ERROR("❌ [%s] SourcePlace must include 'Name'", filename);
          return false;
        }

        if (get_child_by_tag(sp, "Signal"))
        {
          LOG_WARN("⚠️ [%s] SourcePlace contains unexpected 'Signal' — ignored at parse time", filename);
        }
      }
    }
    else if (strcmp(subtag, "DestinationList") == 0)
    {
      if (is_definition && seen_dest)
      {
        LOG_ERROR("❌ [%s] Definition: Only one DestinationList allowed", filename);
        return false;
      }
      if (is_definition && seen_source == false)
      {
        LOG_ERROR("❌ [%s] Definition: DestinationList must come after SourceList", filename);
        return false;
      }
      if (is_invocation && seen_dest)
      {
        LOG_ERROR("❌ [%s] Invocation: Only one DestinationList allowed", filename);
        return false;
      }
      if (is_invocation && seen_source)
      {
        LOG_ERROR("❌ [%s] Invocation: DestinationList must come before SourceList", filename);
        return false;
      }

      seen_dest = true;

      for (size_t j = 1; j < child->count; ++j)
      {
        SExpr *dp = child->list[j];
        if (!dp || dp->type != S_EXPR_LIST)
          continue;

        bool has_name = get_child_by_tag(dp, "Name") != NULL;
        bool has_signal = get_child_by_tag(dp, "Signal") != NULL;

        if (is_invocation)
        {
          if (!has_signal)
          {
            LOG_ERROR("❌ [%s] Invocation: DestinationPlace must include 'Signal'", filename);
            return false;
          }
          if (has_name)
          {
            LOG_WARN("⚠️ [%s] Invocation: DestinationPlace has unnecessary 'Name' — ignored", filename);
          }
        }
        else if (is_definition)
        {
          if (!has_name)
          {
            LOG_ERROR("❌ [%s] Definition: DestinationPlace must include 'Name'", filename);
            return false;
          }
          if (has_signal)
          {
            LOG_ERROR("❌ [%s] Definition: DestinationPlace must not include 'Signal'", filename);
            return false;
          }
        }
      }
    }
    else if (strcmp(subtag, "ConditionalInvocation") == 0)
    {
      SExpr *output = get_child_by_tag(child, "Output");
      if (!output || output->count != 2 || output->list[1]->type != S_EXPR_ATOM)
      {
        LOG_ERROR("❌ [%s] ConditionalInvocation missing valid (Output name)", filename);
        return false;
      }
    }
  }

  if (!seen_dest || !seen_source)
  {
    LOG_ERROR("❌ [%s] Must include both DestinationList and SourceList", filename);
    return false;
  }

  LOG_INFO("✅ [%s] %s validated successfully", filename, is_invocation ? "Invocation" : "Definition");
  return true;
}

void validate_invocation_wiring(Block *blk)
{
  LOG_INFO("🧪 Validating invocation wiring...");

  for (Invocation *inv = blk->invocations; inv; inv = inv->next)
  {
    for (SourcePlace *sp = inv->sources; sp; sp = sp->next)
    {
      const char *sig_name = sp->resolved_name ? sp->resolved_name : sp->name;
      if (!sp->signal || sp->signal == &NULL_SIGNAL || !sp->signal->content)
      {
        LOG_WARN(
            "⚠️ Invocation '%s': SourcePlace '%s' is unwired or missing content",
            inv->name, sig_name);
      }
      else
      {
        LOG_INFO("✅ Invocation '%s': SourcePlace '%s' has content [%s]",
                 inv->name, sig_name, sp->signal->content);
      }
    }

    for (DestinationPlace *dp = inv->destinations; dp; dp = dp->next)
    {
      const char *sig_name = dp->resolved_name ? dp->resolved_name : dp->name;
      if (!dp->signal || dp->signal == &NULL_SIGNAL || !dp->signal->content)
      {
        LOG_WARN("⚠️ Invocation '%s': DestinationPlace '%s' is unwired or "
                 "missing content",
                 inv->name, sig_name);
      }
      else
      {
        LOG_INFO("✅ Invocation '%s': DestinationPlace '%s' has content [%s]",
                 inv->name, sig_name, dp->signal->content);
      }
    }
  }
}

int get_next_instance_id(const char *name)
{
  for (NameCounter *nc = counters; nc; nc = nc->next)
  {
    if (strcmp(nc->name, name) == 0)
    {
      return ++nc->count;
    }
  }
  NameCounter *nc = calloc(1, sizeof(NameCounter));
  nc->name = strdup(name);
  nc->count = 0;
  nc->next = counters;
  counters = nc;
  return 0;
}

int rewrite_signals(Block *blk)
{
  LOG_INFO("🧪 Starting global signal name rewriting pass...");

  for (Invocation *inv = blk->invocations; inv != NULL; inv = inv->next)
  {
    int id = get_next_instance_id(inv->name);
    inv->instance_id = id;

    // Rewrite SourcePlaces
    for (SourcePlace *src = inv->sources; src != NULL; src = src->next)
    {
      char *new_name;
      asprintf(&new_name, "%s.%d.%s", inv->name, id, src->name);
      src->resolved_name = new_name;
      LOG_INFO("🔁 SourcePlace rewritten: %s → %s", src->name, new_name);
    }

    // Rewrite DestinationPlaces
    int param_index = 0;
    for (DestinationPlace *dst = inv->destinations; dst != NULL; dst = dst->next, param_index++)
    {
      if (!dst->name)
      {
        char synth[64];
        snprintf(synth, sizeof(synth), "%s.%d.%d", inv->name, id, param_index);
        dst->name = strdup(synth);
        LOG_INFO("💡 Synthesized destination name: %s", dst->name);
      }

      char *new_name;
      if (strncmp(dst->name, inv->name, strlen(inv->name)) == 0)
      {
        // Already prefixed
        dst->resolved_name = strdup(dst->name);
        LOG_INFO("✅ Skipping double-prefix: %s", dst->resolved_name);
      }
      else
      {
        asprintf(&new_name, "%s.%d.%s", inv->name, id, dst->name);
        dst->resolved_name = new_name;
        LOG_INFO("🔁 DestinationPlace rewritten: %s → %s", dst->name, new_name);
      }
    }
  }

  for (Definition *def = blk->definitions; def != NULL; def = def->next)
  {
    for (SourcePlace *src = def->sources; src != NULL; src = src->next)
    {
      char *new_name;
      asprintf(&new_name, "%s.local.%s", def->name, src->name);
      src->resolved_name = new_name;
      LOG_INFO("🔁 Definition SourcePlace rewritten: %s → %s", src->name, new_name);
    }

    for (DestinationPlace *dst = def->destinations; dst != NULL; dst = dst->next)
    {
      char *new_name;
      asprintf(&new_name, "%s.local.%s", def->name, dst->name);
      dst->resolved_name = new_name;
      LOG_INFO("🔁 Definition DestinationPlace rewritten: %s → %s", dst->name, new_name);
    }

    if (def->conditional_invocation && def->conditional_invocation->output)
    {
      char *new_output;
      asprintf(&new_output, "%s.local.%s", def->name, def->conditional_invocation->output);
      def->conditional_invocation->resolved_output = new_output;
      LOG_INFO("🔁 ConditionalInvocation Output rewritten: %s → %s", def->conditional_invocation->output, new_output);
    }

    for (Invocation *inv = def->invocations; inv != NULL; inv = inv->next)
    {
      for (SourcePlace *src = inv->sources; src != NULL; src = src->next)
      {
        char *new_name;
        asprintf(&new_name, "%s.%s.%s", def->name, inv->name, src->name);
        src->resolved_name = new_name;
        LOG_INFO("🔁 Nested SourcePlace rewritten: %s → %s", src->name, new_name);
      }

      for (DestinationPlace *dst = inv->destinations; dst != NULL; dst = dst->next)
      {
        char *new_name;
        asprintf(&new_name, "%s.%s.%s", def->name, inv->name, dst->name);
        dst->resolved_name = new_name;
        LOG_INFO("🔁 Nested DestinationPlace rewritten: %s → %s", dst->name, new_name);
      }
    }

    ConditionalInvocation *ci = def->conditional_invocation;
    if (ci && ci->arg_count > 0 && ci->template_args)
    {
      for (size_t i = 0; i < ci->arg_count; ++i)
      {
        const char *raw_arg = ci->template_args[i];
        for (SourcePlace *src = def->sources; src != NULL; src = src->next)
        {
          if (src->name && strcmp(src->name, raw_arg) == 0)
          {
            if (ci->resolved_template_args[i])
              free(ci->resolved_template_args[i]);

            ci->resolved_template_args[i] = strdup(src->resolved_name);
            LOG_INFO("🔁 ConditionalInvocation Arg rewritten: %s → %s", raw_arg, ci->resolved_template_args[i]);
            break;
          }
        }
      }
    }
  }

  // 🔚 Cleanup counters
  while (counters)
  {
    NameCounter *next = counters->next;
    free((void *)counters->name);
    free(counters);
    counters = next;
  }

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
    if (!validate_sexpr(expr, path))
    {
      LOG_ERROR("❌ Validation failed for: %s", path);
      free_sexpr(expr);
      exit(1);
    }

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

static struct SExpr *parse_expr(const char **input)
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

static void emit_source_list(FILE *out, SourcePlace *src, int indent)
{
  emit_indent(out, indent);
  fputs("(SourceList\n", out);

  for (; src; src = src->next)
  {
    emit_indent(out, indent + 2);
    fputs("(SourcePlace\n", out);

    const char *signame = src->resolved_name ? src->resolved_name : src->name;
    if (signame)
    {
      emit_indent(out, indent + 4);
      fputs("(Name ", out);
      emit_atom(out, signame);
      fputs(")\n", out);
    }

    if (src->signal && src->signal != &NULL_SIGNAL && src->signal->content)
    {
      LOG_INFO(
          "✏️ Emitting SourcePlace: name=%s signal=%s",
          signame,
          (src->signal && src->signal != &NULL_SIGNAL && src->signal->content)
              ? src->signal->content
              : "null");

      emit_indent(out, indent + 4);
      fputs("(Signal\n", out);
      emit_indent(out, indent + 6);
      fputs("(Content ", out);
      emit_atom(out, src->signal->content);
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

static void emit_destination_list(FILE *out, DestinationPlace *dst,
                                  int indent)
{
  emit_indent(out, indent);
  fputs("(DestinationList\n", out);

  for (; dst; dst = dst->next)
  {
    emit_indent(out, indent + 2);
    fputs("(DestinationPlace\n", out);

    const char *signame = dst->resolved_name ? dst->resolved_name : dst->name;
    if (signame)
    {
      emit_indent(out, indent + 4);
      fputs("(Name ", out);
      emit_atom(out, signame);
      fputs(")\n", out);
    }

    if (dst->signal && dst->signal != &NULL_SIGNAL && dst->signal->content)
    {
      emit_indent(out, indent + 4);
      fputs("(Signal\n", out);
      emit_indent(out, indent + 6);
      fputs("(Content ", out);
      emit_atom(out, dst->signal->content);
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

static void emit_conditional(FILE *out, ConditionalInvocation *ci, int indent)
{
  emit_indent(out, indent);
  fputs("(ConditionalInvocation\n", out);

  // Emit Output if present
  if (ci->resolved_output)
  {
    emit_indent(out, indent + 2);
    fprintf(out, "(Output %s)\n", ci->resolved_output);
  }

  // Emit Resolved Template
  emit_indent(out, indent + 2);
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

static void emit_invocation(FILE *out, Invocation *inv, int indent)
{
  emit_indent(out, indent);
  fputs("(Invocation\n", out);

  emit_indent(out, indent + 2);
  fputs("(Name ", out);
  emit_atom(out, inv->name);
  fputs(")\n", out);

  // ⚠️ This is the correct order per spec:
  emit_destination_list(out, inv->destinations, indent + 2);
  emit_source_list(out, inv->sources, indent + 2);

  emit_indent(out, indent);
  fputs(")\n", out);
}

void write_definition_to_file(Definition *def, FILE *out, int indent)
{
  emit_indent(out, indent);
  fputs("(Definition\n", out);

  emit_indent(out, indent + 2);
  fputs("(Name ", out);
  emit_atom(out, def->name);
  fputs(")\n", out);

  emit_source_list(out, def->sources, indent + 2);
  emit_destination_list(out, def->destinations, indent + 2);

  if (def->conditional_invocation)
  {
    emit_conditional(out, def->conditional_invocation, indent + 2);
  }

  for (Invocation *inv = def->invocations; inv; inv = inv->next)
  {
    emit_invocation(out, inv, indent + 2);
  }

  emit_indent(out, indent);
  fputs(")\n", out);
}

void write_definition_to_path(Definition *def, const char *filepath)
{
  FILE *out = fopen(filepath, "w");
  if (!out)
  {
    fprintf(stderr, "❌ Failed to open file for writing: %s\n", filepath);
    return;
  }

  write_definition_to_file(def, out, 0);
  fclose(out);
  LOG_INFO("✅ Wrote definition '%s' to %s", def->name, filepath);
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
    write_definition_to_path(def, path);
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
