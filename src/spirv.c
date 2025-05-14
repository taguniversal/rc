#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "eval.h"
#include "log.h"
#include <vulkan/vulkan.h>
#include <assert.h>
#include "spirv.h"
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

void emit_conditional_invocation(SPIRVModule *mod, const char *prefix, ConditionalInvocation *ci)
{
  char tmp[128]; // Increased buffer for prefix usage

  if (!mod || !ci || !ci->arg_count || !ci->template_args || !ci->cases)
  {
    LOG_ERROR("❌ Invalid ConditionalInvocation for prefix %s", prefix);
    return;
  }

  LOG_INFO("📊 Emitting SPIR-V for invocation with prefix: %s", prefix);

  // ─── 1. Function Setup ───────────────────────────────────────────────
  emit_op(mod, "OpFunction", (const char *[]){"%void", "None", "%void_fn"}, 3);
  snprintf(tmp, sizeof(tmp), "%%%sentry", prefix);
  emit_op(mod, "OpLabel", (const char *[]){tmp}, 1);

  emit_op(mod, "OpConstant", (const char *[]){"%bool", "%true", "1"}, 3);
  emit_op(mod, "OpConstant", (const char *[]){"%bool", "%false", "0"}, 3);

  // ─── 2. Input Variables and Loads ────────────────────────────────────
  for (size_t i = 0; i < ci->arg_count; ++i)
  {
    const char *arg = ci->template_args[i];

    snprintf(tmp, sizeof(tmp), "%%%svar_%s", prefix, arg);
    char *var_id = strdup(tmp);
    emit_op(mod, "OpVariable", (const char *[]){var_id, "%bool", "Input"}, 3);

    snprintf(tmp, sizeof(tmp), "%%%s%s", prefix, arg);
    char *load_id = strdup(tmp);
    emit_op(mod, "OpLoad", (const char *[]){load_id, "%bool", var_id}, 3);

    free(var_id);
    free(load_id);
  }

  // After emitting all input variables and loads:
  snprintf(tmp, sizeof(tmp), "%%%s_var_out", prefix);
  emit_op(mod, "OpVariable", (const char *[]){tmp, "%bool", "Output"}, 3);

  // ─── 3. Emit Each Case ───────────────────────────────────────────────
  char acc_result[128] = "%false"; // Accumulator starts false
  bool has_result = false;

  int case_idx = 0;
  for (ConditionalInvocationCase *c = ci->cases; c != NULL; c = c->next, ++case_idx)
  {
    if (!c->pattern || !c->result)
    {
      LOG_ERROR("❌ Malformed case %d in prefix %s", case_idx, prefix);
      continue;
    }

    size_t n = strlen(c->pattern);
    char *and_result = NULL;

    for (size_t i = 0; i < n; ++i)
    {
      const char *bit = (c->pattern[i] == '1') ? "%true" : "%false";

      snprintf(tmp, sizeof(tmp), "%%%scmp_%d_%zu", prefix, case_idx, i);
      char *cmp = strdup(tmp);

      snprintf(tmp, sizeof(tmp), "%%%s%s", prefix, ci->template_args[i]);
      char *input = strdup(tmp);

      emit_op(mod, "OpIEqual", (const char *[]){cmp, "%bool", input, bit}, 4);

      if (i == 0)
      {
        and_result = strdup(cmp);
      }
      else
      {
        snprintf(tmp, sizeof(tmp), "%%%sand_%d_%zu", prefix, case_idx, i);
        char *next_and = strdup(tmp);
        emit_op(mod, "OpLogicalAnd", (const char *[]){next_and, and_result, cmp}, 3);
        free(and_result);
        and_result = next_and;
      }

      free(cmp);
      free(input);
    }

    snprintf(tmp, sizeof(tmp), "%%%scase_%d_result", prefix, case_idx);
    char *sel_result = strdup(tmp);

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
      snprintf(tmp, sizeof(tmp), "%%%sresult_%d", prefix, case_idx);
      char *next_result = strdup(tmp);
      emit_op(mod, "OpLogicalOr", (const char *[]){next_result, "%bool", acc_result, sel_result}, 4);
      snprintf(acc_result, sizeof(acc_result), "%s", tmp);
      free(next_result);
    }

    free(sel_result);
  }

  // ─── 4. Output ───────────────────────────────────────────────────────
  snprintf(tmp, sizeof(tmp), "%%%s_var_out", prefix); // use actual variable name
  emit_op(mod, "OpStore", (const char *[]){tmp, acc_result}, 2);

  emit_op(mod, "OpReturn", NULL, 0);
  emit_op(mod, "OpFunctionEnd", NULL, 0);
}

int get_definition_instance_id(const char *name)
{
  for (UsageEntry *e = usage_head; e; e = e->next)
  {
    if (strcmp(e->def_name, name) == 0)
    {
      return e->count++;
    }
  }
  // First time seeing this definition
  UsageEntry *new_entry = calloc(1, sizeof(UsageEntry));
  new_entry->def_name = strdup(name);
  new_entry->count = 1;
  new_entry->next = usage_head;
  usage_head = new_entry;
  return 0;
}

void spirv_parse_block(Block *blk, const char *spirv_out_dir)
{

  for (Definition *def = blk->definitions; def != NULL; def = def->next)
  {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.spvasm.sexpr", spirv_out_dir, def->name);

    FILE *f = fopen(path, "w");
    if (!f)
    {
      LOG_ERROR("❌ Failed to open SPIR-V output file for %s", def->name);
      continue;
    }

    LOG_INFO("🌀 Generating SPIR-V S-expression for %s", def->name);
    int instance_id = get_definition_instance_id(def->name);
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "%s_%d_", def->name, instance_id);

    SPIRVModule mod = {0};
    emit_spirv_header(&mod);

    if (def->conditional_invocation)
    {
      emit_conditional_invocation(&mod, prefix, def->conditional_invocation);
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
