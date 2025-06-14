#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "log.h"
#include <vulkan/vulkan.h>
#include <assert.h>
#include "emit_spirv.h"
#include "emit_util.h"
#include "spirv_passes.h"

int id_counter = 1;
int next_id(void)
{
  return id_counter++;
}

bool opcode_has_result_id(const char *opcode)
{
  return strcmp(opcode, "OpStore") != 0 &&
         strcmp(opcode, "OpReturn") != 0 &&
         strcmp(opcode, "OpFunctionEnd") != 0 &&
         strcmp(opcode, "OpBranch") != 0 &&
         strcmp(opcode, "OpLabel") != 0 &&
         strcmp(opcode, "OpLoopMerge") != 0 &&
         strcmp(opcode, "OpSelectionMerge") != 0;
}

static SignalIDEntry *signal_registry = NULL;

void register_external_signal(const char *signal, const char *id)
{
  SignalIDEntry *entry = calloc(1, sizeof(SignalIDEntry));
  entry->signal = strdup(signal);
  entry->id = strdup(id);
  entry->next = signal_registry;
  signal_registry = entry;
}

const char *lookup_internal_id(const char *signal)
{
  for (SignalIDEntry *entry = signal_registry; entry != NULL; entry = entry->next)
  {
    if (strcmp(entry->signal, signal) == 0)
    {
      return entry->id;
    }
  }
  LOG_WARN("⚠️ lookup_internal_id: Signal not found: %s", signal);
  return NULL;
}

// For deduplicating SPIR-V ops
typedef struct UsageEntry
{
  const char *def_name;
  int count;
  struct UsageEntry *next;
} UsageEntry;

UsageEntry *usage_head = NULL;

SPIRVModule *spirv_module_new(void)
{
  SPIRVModule *mod = calloc(1, sizeof(SPIRVModule));
  return mod;
}

void emit_op(SPIRVModule *mod, const char *opcode, const char **operands, size_t count)
{
  SPIRVOp *op = make_op(opcode, operands, count);
  if (!mod->head)
  {
    mod->head = mod->tail = op;
  }
  else
  {
    mod->tail->next = op;
    mod->tail = op;
  }
}

void free_spirv_module(SPIRVModule *mod)
{
  if (!mod)
    return;
  SPIRVOp *op = mod->head;
  while (op)
  {
    SPIRVOp *next = op->next;
    free(op->opcode);
    for (size_t i = 0; i < op->operand_count; ++i)
    {
      free(op->operands[i]);
    }
    free(op->operands);
    free(op);
    op = next;
  }
  mod->head = NULL;
  mod->tail = NULL;
}

SPIRVOp *make_op(const char *opcode, const char **operands, size_t count)
{
  SPIRVOp *op = calloc(1, sizeof(SPIRVOp));
  op->opcode = strdup(opcode);
  op->operand_count = count;
  op->operands = calloc(count, sizeof(char *));

  size_t start_index = 0;

  if (opcode_has_result_id(opcode))
  {
    // First operand is result ID
    op->result_id = strdup(operands[0]);
    start_index = 1;
    op->operand_count--; // shift to exclude the result_id
  }
  else
  {
    op->result_id = NULL;
  }

  for (size_t i = 0; i < op->operand_count; ++i)
  {
    op->operands[i] = strdup(operands[start_index + i]);
  }

  return op;
}

void write_spirv_op(FILE *out, SPIRVOp *op, int indent)
{
  if (op->result_id)
  {
    // Format: (%result_id = OpX ...)
    fprintf(out, "%*s%s = (%s", indent, "", op->result_id, op->opcode);
  }
  else
  {
    // Format: (OpX ...)
    fprintf(out, "%*s(%s", indent, "", op->opcode);
  }

  for (size_t i = 0; i < op->operand_count; ++i)
  {
    fprintf(out, " %s", op->operands[i]);
  }

  fprintf(out, ")\n");
}

void write_spirv_module(FILE *out, SPIRVOp *head)
{
  for (SPIRVOp *op = head; op; op = op->next)
  {
    write_spirv_op(out, op, 0);
  }
}

SPIRVOp *build_sample_spirv(void)
{
  SPIRVOp *head = NULL, *tail = NULL;

  const char *op1[] = {"Shader"};
  const char *op2[] = {"Logical", "GLSL450"};
  const char *op3[] = {"%int", "32", "1"};
  const char *op4[] = {"%void"};
  const char *op5[] = {"%void_fn", "%void"};

  SPIRVOp *ops[] = {
      make_op("OpCapability", op1, 1),
      make_op("OpMemoryModel", op2, 2),
      make_op("OpTypeInt", op3, 3),
      make_op("OpTypeVoid", op4, 1),
      make_op("OpTypeFunction", op5, 2)};

  for (int i = 0; i < 5; ++i)
  {
    if (!head)
      head = tail = ops[i];
    else
    {
      tail->next = ops[i];
      tail = ops[i];
    }
  }

  return head;
}

void emit_spirv_header(SPIRVModule *mod)
{
  emit_op(mod, "OpCapability", (const char *[]){"Shader"}, 1);
  emit_op(mod, "OpMemoryModel", (const char *[]){"Logical", "GLSL450"}, 2);
  emit_op(mod, "OpTypeInt", (const char *[]){"%int", "32", "1"}, 3);
  emit_op(mod, "OpTypeVoid", (const char *[]){"%void"}, 1);
  emit_op(mod, "OpTypeFunction", (const char *[]){"%void_fn", "%void"}, 2);
}

void emit_conditional_invocation(SPIRVModule *mod, ConditionalInvocation *ci)
{
  if (!mod || !ci || !ci->arg_count || !ci->pattern_args || !ci->cases)
  {
    LOG_ERROR("❌ Invalid ConditionalInvocation input");
    return;
  }

  Invocation *inv = mod->current_invocation;
  if (!inv)
  {
    LOG_ERROR("❌ No current invocation set on module");
    return;
  }

  LOG_INFO("📊 Emitting SPIR-V for invocation: %s", inv->target_name);

  // ─── 1. Function Setup ───────────────────────────────────────────────
  emit_op(mod, "OpFunction", (const char *[]){"%void", "None", "%void_fn"}, 3);
  emit_op(mod, "OpLabel", (const char *[]){"%entry"}, 1);

  emit_op(mod, "OpConstant", (const char *[]){"%bool", "%true", "1"}, 3);
  emit_op(mod, "OpConstant", (const char *[]){"%bool", "%false", "0"}, 3);

  // ─── 2. Input Variables and Loads ────────────────────────────────────
  for (size_t i = 0; i < ci->arg_count; ++i)
  {
    const char *arg_name = string_list_get_by_index(ci->pattern_args, i);
    if (!arg_name)
    {
      LOG_WARN("⚠️ Missing pattern arg[%zu] during input loading", i);
      continue;
    }

    for (size_t j = 0; j < string_list_count(inv->input_signals); ++j)
    {
      const char *resolved = string_list_get_by_index(inv->input_signals, j);
      if (resolved && strstr(resolved, arg_name))
      {
        char tmp[128];
        snprintf(tmp, sizeof(tmp), "%%%s", resolved);
        const char *id = lookup_internal_id(resolved);
        emit_op(mod, "OpLoad", (const char *[]){tmp, "%bool", id}, 3);
        break;
      }
    }
  }

  // ─── 3. Output Variable ──────────────────────────────────────────────
  emit_op(mod, "OpVariable", (const char *[]){"%out_var", "%bool", "Output"}, 3);

  for (size_t i = 0; i < string_list_count(inv->output_signals); ++i)
  {
    const char *sig = string_list_get_by_index(inv->output_signals, i);
    if (sig)
    {
      LOG_INFO("🔁 Registering output signal: %s => %%out_var", sig);
      register_external_signal(sig, "%out_var");
    }
  }

  // ─── 4. Emit Each Case ───────────────────────────────────────────────
  char acc_result[128] = "%false";
  bool has_result = false;

  for (size_t case_idx = 0; case_idx < ci->case_count; ++case_idx)
  {
    ConditionalCase *c = &ci->cases[case_idx];
    if (!c->pattern || !c->result)
      continue;

    char *and_result = NULL;

    for (size_t i = 0; i < ci->arg_count; ++i)
    {
      const char *bit = (c->pattern[i] == '1') ? "%true" : "%false";

      char input_var[128];
      snprintf(input_var, sizeof(input_var), "%%fallback");

      const char *arg_name = string_list_get_by_index(ci->pattern_args, i);
      if (!arg_name)
        continue;

      for (size_t j = 0; j < string_list_count(inv->input_signals); ++j)
      {
        const char *candidate = string_list_get_by_index(inv->input_signals, j);
        if (candidate && strstr(candidate, arg_name))
        {
          snprintf(input_var, sizeof(input_var), "%%%s", candidate);
          break;
        }
      }

      char cmp_var[128];
      snprintf(cmp_var, sizeof(cmp_var), "%%cmp_%zu_%zu", case_idx, i);
      emit_op(mod, "OpIEqual", (const char *[]){cmp_var, "%bool", input_var, bit}, 4);

      if (!and_result)
      {
        and_result = strdup(cmp_var);
      }
      else
      {
        char next_and[128];
        snprintf(next_and, sizeof(next_and), "%%and_%zu_%zu", case_idx, i);
        emit_op(mod, "OpLogicalAnd", (const char *[]){next_and, and_result, cmp_var}, 3);
        free(and_result);
        and_result = strdup(next_and);
      }
    }

    char sel_result[128];
    snprintf(sel_result, sizeof(sel_result), "%%case_%zu_result", case_idx);
    const char *value = (strcmp(c->result, "1") == 0) ? "%true" : "%false";
    emit_op(mod, "OpSelect", (const char *[]){sel_result, "%bool", and_result, value, "%false"}, 5);
    free(and_result);

    if (!has_result)
    {
      snprintf(acc_result, sizeof(acc_result), "%s", sel_result);
      has_result = true;
    }
    else
    {
      char tmp[128];
      snprintf(tmp, sizeof(tmp), "%%result_%zu", case_idx);
      emit_op(mod, "OpLogicalOr", (const char *[]){tmp, "%bool", acc_result, sel_result}, 4);
      snprintf(acc_result, sizeof(acc_result), "%s", tmp);
    }
  }

  // ─── 5. Store Result ─────────────────────────────────────────────────
  emit_op(mod, "OpStore", (const char *[]){"%out_var", acc_result}, 2);
  emit_op(mod, "OpReturn", NULL, 0);
  emit_op(mod, "OpFunctionEnd", NULL, 0);
}

void spirv_parse_block(Block *blk, const char *spirv_out_dir)
{
  for (Definition *def = blk->definitions; def != NULL; def = def->next)
  {
    // Register signals from conditional invocation
    if (def->conditional_invocation)
    {
      ConditionalInvocation *ci = def->conditional_invocation;

      // Register each signal mentioned in the pattern args
      for (size_t i = 0; i < ci->arg_count; ++i)
      {
        const char *arg = string_list_get_by_index(ci->pattern_args, i); // ✅

        if (!arg)
          continue;

        char *resolved_signal;
        asprintf(&resolved_signal, "%s.local.%s", def->name, arg);
        register_external_signal(resolved_signal, resolved_signal); // maps to itself for now
      }

      // Register output signal
      if (ci->output)
      {
        char *resolved_output;
        asprintf(&resolved_output, "%s.local.%s", def->name, ci->output);
        register_external_signal(resolved_output, resolved_output);
      }
    }

    // Output file path
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.spvasm.sexpr", spirv_out_dir, def->name);

    FILE *f = fopen(path, "w");
    if (!f)
    {
      LOG_ERROR("❌ Failed to open SPIR-V output file for %s", def->name);
      continue;
    }

    LOG_INFO("🌀 Generating SPIR-V S-expression for %s", def->name);

    SPIRVModule mod = {0};
    mod.current_definition = def;

    emit_spirv_header(&mod);

    if (def->conditional_invocation)
    {
      emit_conditional_invocation(&mod, def->conditional_invocation);
    }

    dedupe_prelude_ops(&mod);
    write_spirv_module(f, mod.head);
    fclose(f);
    free_spirv_module(&mod);

    LOG_INFO("✅ Wrote SPIR-V to %s", path);
  }
}

void emit_spirv_block(Block *blk, const char *spirv_frag_dir, const char *out_path)
{
  FILE *out = fopen(out_path, "w");
  if (!out)
  {
    LOG_ERROR("❌ Failed to open output SPIR-V file: %s", out_path);
    return;
  }

  // Emit preamble
  fprintf(out, "(OpCapability Shader)\n");
  fprintf(out, "(OpMemoryModel Logical GLSL450)\n");

  fprintf(out, "(OpEntryPoint GLCompute %%main \"main\")\n");
  fprintf(out, "(OpTypeVoid %%void)\n");
  fprintf(out, "(OpTypeFunction %%void_fn %%void)\n");
  fprintf(out, "(OpFunction %%main %%void None %%void_fn)\n");

  fprintf(out, "(OpLabel %%entry)\n");

  // Concatenate fragments
  for (Definition *def = blk->definitions; def; def = def->next)
  {
    char frag_path[512];
    snprintf(frag_path, sizeof(frag_path), "%s/%s.spvasm.sexpr", spirv_frag_dir, def->name);
    FILE *frag = fopen(frag_path, "r");
    if (!frag)
    {
      LOG_WARN("⚠️ Could not open fragment for %s", def->name);
      continue;
    }

    char line[512];
    while (fgets(line, sizeof(line), frag))
    {
      if (
          strstr(line, "OpCapability") ||
          strstr(line, "OpMemoryModel") ||
          strstr(line, "OpTypeInt") ||
          strstr(line, "OpTypeVoid") ||
          strstr(line, "OpTypeFunction") ||
          strstr(line, "OpConstant") ||
          strstr(line, "OpFunction") ||
          strstr(line, "OpLabel") ||
          strstr(line, "OpFunctionEnd"))
      {
        continue;
      }
      fputs(line, out);
    }

    fclose(frag);
  }

  // Emit return and function end
  fprintf(out, "(OpReturn)\n");
  fprintf(out, "(OpFunctionEnd)\n");

  fclose(out);
  LOG_INFO("✅ Emitted unified SPIR-V block to %s", out_path);
}
