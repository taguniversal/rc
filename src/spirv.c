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

int id_counter = 1;
int next_id(void)
{
  return id_counter++;
}

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
  for (size_t i = 0; i < count; ++i)
  {
    op->operands[i] = strdup(operands[i]);
  }
  return op;
}

void write_spirv_op(FILE *out, SPIRVOp *op, int indent)
{
  fprintf(out, "%*s(%s", indent, "", op->opcode);
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

void emit_conditional_invocation(SPIRVModule *mod, const char *func_name, ConditionalInvocation *ci)
{
  char tmp[64];
  LOG_INFO("📊 Checking ConditionalInvocation for %s", func_name);
  LOG_INFO("    ci pointer: %p", (void *)ci);
  if (ci)
  {
    LOG_INFO("    arg_count: %zu", ci->arg_count);
    LOG_INFO("    template_args: %p", (void *)ci->template_args);
    LOG_INFO("    cases: %p", (void *)ci->cases);
  }

  if (!mod || !ci || !ci->arg_count || !ci->template_args || !ci->cases)
  {
    LOG_ERROR("❌ Invalid ConditionalInvocation for %s", func_name);
    return;
  }

  fprintf(stderr, "DEBUG: Entering emit_conditional_invocation for %s\n", func_name);
  fprintf(stderr, "  arg_count = %zu\n", ci->arg_count);
  for (size_t i = 0; i < ci->arg_count; ++i)
    fprintf(stderr, "    arg[%zu] = %s\n", i, ci->template_args[i]);

  for (ConditionalInvocationCase *c = ci->cases; c != NULL; c = c->next)
    fprintf(stderr, "    case: pattern=%s result=%s\n", c->pattern, c->result);

  // ─── 1. Function Setup ───────────────────────────────────────────────
  emit_op(mod, "OpFunction", (const char *[]){"%void", "None", "%void_fn"}, 3);
  emit_op(mod, "OpLabel", (const char *[]){"%entry"}, 1);

  emit_op(mod, "OpConstant", (const char *[]){"%bool", "%true", "1"}, 3);
  emit_op(mod, "OpConstant", (const char *[]){"%bool", "%false", "0"}, 3);

  // ─── 2. Input Variables and Loads ────────────────────────────────────
  for (size_t i = 0; i < ci->arg_count; ++i)
  {
    const char *arg = ci->template_args[i];

    if (!arg)
    {
      LOG_ERROR("❌ Template arg[%zu] is NULL in %s", i, func_name);
      continue;
    }
    snprintf(tmp, sizeof(tmp), "%%var_%s", arg);
    char *var_id = strdup(tmp);
    emit_op(mod, "OpVariable", (const char *[]){"%bool", var_id, "Input"}, 3);

    snprintf(tmp, sizeof(tmp), "%%%s", arg);
    char *load_id = strdup(tmp);
    emit_op(mod, "OpLoad", (const char *[]){load_id, var_id}, 2);

    free(var_id);
    free(load_id);
  }

  // ─── 3. Emit Each Case ───────────────────────────────────────────────

  char acc_result[64] = "%false"; // start from %false literal — avoid strdup
  bool has_result = false;

  int case_idx = 0;
  for (ConditionalInvocationCase *c = ci->cases; c != NULL; c = c->next, ++case_idx)
  {
    if (!c->pattern || !c->result)
    {
      LOG_ERROR("❌ Malformed case %d in %s", case_idx, func_name);
      continue;
    }
    size_t n = strlen(c->pattern);
    char *and_result = NULL;

    // Build conjunction of all equality checks
    for (size_t i = 0; i < n; ++i)
    {
      const char *bit = (c->pattern[i] == '1') ? "%true" : "%false";

      snprintf(tmp, sizeof(tmp), "%%cmp_%d_%zu", case_idx, i);
      char *cmp = strdup(tmp);

      snprintf(tmp, sizeof(tmp), "%%%s", ci->template_args[i]);
      char *input = strdup(tmp);

      emit_op(mod, "OpIEqual", (const char *[]){cmp, input, bit}, 3);

      if (i == 0)
      {
        and_result = strdup(cmp);
      }
      else
      {
        snprintf(tmp, sizeof(tmp), "%%and_%d_%zu", case_idx, i);
        char *next_and = strdup(tmp);
        emit_op(mod, "OpLogicalAnd", (const char *[]){next_and, and_result, cmp}, 3);
        free(and_result);
        and_result = next_and;
      }

      free(cmp);
      free(input);
    }

    // Emit OpSelect with result literal
    snprintf(tmp, sizeof(tmp), "%%case_%d_result", case_idx);
    char *sel_result = strdup(tmp);

    const char *value = (strcmp(c->result, "1") == 0) ? "%true" : "%false";
    emit_op(mod, "OpSelect", (const char *[]){sel_result, and_result, value, "%false"}, 4);
    free(and_result);

    // Accumulate with OR
    if (!has_result)
    {
      snprintf(acc_result, sizeof(acc_result), "%s", sel_result);
      has_result = true;
    }
    else
    {
      snprintf(tmp, sizeof(tmp), "%%result_%d", case_idx);
      char *next_result = strdup(tmp);
      emit_op(mod, "OpLogicalOr", (const char *[]){next_result, acc_result, sel_result}, 3);
      snprintf(acc_result, sizeof(acc_result), "%s", tmp); // overwrite acc_result with new result id
      free(next_result);
    }
    free((void *)sel_result);
  }

  // ─── 4. Final Output Store ───────────────────────────────────────────
  emit_op(mod, "OpStore", (const char *[]){"%out", acc_result}, 2);
  emit_op(mod, "OpReturn", NULL, 0);
  emit_op(mod, "OpFunctionEnd", NULL, 0);

  // free(acc_result);
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

    SPIRVModule mod = {0};
    emit_spirv_header(&mod);

    if (def->conditional_invocation)
    {
      emit_conditional_invocation(&mod, def->name, def->conditional_invocation);
    }

    write_spirv_module(f, mod.head);
    fclose(f);
    free_spirv_module(&mod);

    LOG_INFO("✅ Wrote SPIR-V to %s", path);
  }
}

void emit_spirv_block(Block *blk, const char *spirv_frag_dir, const char *out_path) {
    FILE *out = fopen(out_path, "w");
    if (!out) {
        LOG_ERROR("❌ Failed to open output SPIR-V file: %s", out_path);
        return;
    }

    // Emit preamble
    fprintf(out, "(OpCapability Shader)\n");
    fprintf(out, "(OpMemoryModel Logical GLSL450)\n");
    fprintf(out, "(OpEntryPoint GLCompute %%main \"main\")\n");

    // Emit void type and function wrapper
    fprintf(out, "(OpTypeVoid %%void)\n");
    fprintf(out, "(OpTypeFunction %%void_fn %%void)\n");
    fprintf(out, "(OpFunction %%void None %%void_fn)\n");
    fprintf(out, "(OpLabel %%entry)\n");

    // Concatenate fragments
    for (Definition *def = blk->definitions; def; def = def->next) {
        char frag_path[512];
        snprintf(frag_path, sizeof(frag_path), "%s/%s.spvasm.sexpr", spirv_frag_dir, def->name);
        FILE *frag = fopen(frag_path, "r");
        if (!frag) {
            LOG_WARN("⚠️ Could not open fragment for %s", def->name);
            continue;
        }

        char line[512];
        while (fgets(line, sizeof(line), frag)) {
            // Only emit instructions inside functions
            if (strstr(line, "OpFunction") || strstr(line, "OpLabel") || strstr(line, "OpFunctionEnd"))
                continue;
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

