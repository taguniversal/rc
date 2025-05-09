#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "invocation.h"
#include "log.h"

int id_counter = 1;
int next_id()
{
  return id_counter++;
}

void emit_header(FILE *f)
{
  fprintf(f, "OpCapability Shader\n");
  fprintf(f, "OpMemoryModel Logical GLSL450\n");
}

void emit_types(FILE *f)
{
  fprintf(f, "\n");
  fprintf(f, "%%int = OpTypeInt 32 1\n");
  fprintf(f, "%%void = OpTypeVoid\n");
  fprintf(f, "%%void_fn = OpTypeFunction %%void\n");
  fprintf(f, "%%ptr_int = OpTypePointer Function %%int\n");
}

void emit_function_start(FILE *f, const char *name)
{
  fprintf(f, "\n; Function: %s\n", name);
  fprintf(f, "%%f = OpFunction %%void None %%void_fn\n");
  fprintf(f, "%%entry = OpLabel\n");
}
void emit_invocation(FILE* f, const Invocation* inv) {
    if (!inv) return;

    emit_function_start(f, inv->name);

    const SourcePlace* src = inv->sources;
    int input_index = 0;
    char input_vars[2][32] = {0};

    while (src && input_index < 2) {
        if (src->name) {
            fprintf(f, "%%var_%s = OpVariable %%ptr_int Function\n", src->name);
            fprintf(f, "%%val_%s = OpLoad %%int %%var_%s\n", src->name, src->name);
            snprintf(input_vars[input_index++], 32, "%%val_%s", src->name);
        } else if (src->value) {
            int val = atoi(src->value);
            fprintf(f, "%%const_%d = OpConstant %%int %d\n", val, val);
            snprintf(input_vars[input_index++], 32, "%%const_%d", val);
        } else {
            LOG_WARN("⚠️ Skipping SourcePlace with null name and value in Invocation '%s'", inv->name);
        }
        src = src->next;
    }

    if (input_index == 2) {
        fprintf(f, "%%sum = OpIAdd %%int %s %s\n", input_vars[0], input_vars[1]);
    } else {
        LOG_WARN("⚠️ Not enough valid sources for Invocation '%s'", inv->name);
        fprintf(f, "OpReturn\nOpFunctionEnd\n");
        return;
    }

    const DestinationPlace* dst = inv->destinations;
    if (dst) {
        fprintf(f, "%%var_%s = OpVariable %%ptr_int Function\n", dst->name);
        fprintf(f, "OpStore %%var_%s %%sum\n", dst->name);
    }

    fprintf(f, "OpReturn\nOpFunctionEnd\n");
}


int spirv_parse_block(Block *blk, const char *spirv_dir)
{
  if (!blk || !spirv_dir)
    return 1;

  // Ensure the output directory exists
  mkdir(spirv_dir, 0755);

  Invocation *inv = blk->invocations;
  while (inv)
  {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.spvasm", spirv_dir, inv->name);
    FILE *f = fopen(path, "w");
    if (!f)
    {
      fprintf(stderr, "❌ Failed to open %s for writing\n", path);
      return 1;
    }

    emit_header(f);
    emit_types(f);
    emit_invocation(f, inv);

    fclose(f);
    LOG_INFO("✅ Wrote SPIR-V for %s to %s\n", inv->name, path);

    inv = inv->next;
  }

  return 0;
}
