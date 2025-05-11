#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>

#include "invocation.h"
#include "log.h"
#include <vulkan/vulkan.h>
#include <assert.h>

#define CHECK_VK(result)                                                   \
  if (result != VK_SUCCESS)                                                \
  {                                                                        \
    fprintf(stderr, "❌ Vulkan error at line %d: %d\n", __LINE__, result); \
    exit(1);                                                               \
  }

typedef struct
{
  VkInstance instance;
  VkPhysicalDevice physical_device;
  VkDevice device;
} VulkanContext;

bool init_vulkan(VulkanContext *ctx, bool require_device)
{
   const char *desired[] = {
      "VK_KHR_portability_enumeration",
      "VK_KHR_get_physical_device_properties2",
      "VK_KHR_portability_subset",
  };


  const char *enabled[3];
  uint32_t enabled_count = 0;

  VkApplicationInfo app_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "RealityCompiler",
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
      .pEngineName = "RC",
      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
      .apiVersion = VK_API_VERSION_1_0,
  };

  // Query what's available
  uint32_t ext_count = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &ext_count, NULL);
  VkExtensionProperties ext_props[ext_count];
  vkEnumerateInstanceExtensionProperties(NULL, &ext_count, ext_props);

  // Match against desired
  for (int i = 0; i < 3; ++i)
  {
    for (uint32_t j = 0; j < ext_count; ++j)
    {
      if (strcmp(desired[i], ext_props[j].extensionName) == 0)
      {
        enabled[enabled_count++] = desired[i];
        break;
      }
    }
  }

  LOG_INFO("🔌 Enabling %d Vulkan extensions", enabled_count);
  for (uint32_t i = 0; i < enabled_count; ++i)
    LOG_INFO("✅ Using extension: %s", enabled[i]);

  VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
      .pApplicationInfo = &app_info,
      .enabledExtensionCount = enabled_count,
      .ppEnabledExtensionNames = enabled,
  };

  if (vkCreateInstance(&instance_info, NULL, &ctx->instance) != VK_SUCCESS)
  {
    LOG_ERROR("❌ Failed to create Vulkan instance.");
    return false;
  }

  uint32_t device_count = 0;
  vkEnumeratePhysicalDevices(ctx->instance, &device_count, NULL);
  if (device_count == 0)
  {
    LOG_WARN("⚠️ No Vulkan-compatible GPU found.");
    vkDestroyInstance(ctx->instance, NULL);
    return false;
  }

  VkPhysicalDevice devices[device_count];
  vkEnumeratePhysicalDevices(ctx->instance, &device_count, devices);
  ctx->physical_device = devices[0];

  if (!require_device)
    return true;

  // --- Create logical device ---
  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_device, &queue_family_count, NULL);
  VkQueueFamilyProperties props[queue_family_count];
  vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_device, &queue_family_count, props);

  int compute_family = -1;
  for (uint32_t i = 0; i < queue_family_count; ++i)
  {
    if (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
    {
      compute_family = i;
      break;
    }
  }

  if (compute_family == -1)
  {
    LOG_ERROR("❌ No compute-capable queue family found.");
    vkDestroyInstance(ctx->instance, NULL);
    return false;
  }

  float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = (uint32_t)compute_family,
      .queueCount = 1,
      .pQueuePriorities = &priority,
  };

  VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_info,
  };

  if (vkCreateDevice(ctx->physical_device, &device_info, NULL, &ctx->device) != VK_SUCCESS)
  {
    LOG_ERROR("❌ Failed to create logical device.");
    vkDestroyInstance(ctx->instance, NULL);
    return false;
  }

  return true;
}

int id_counter = 1;
int next_id()
{
  return id_counter++;
}

bool check_spirv_gpu_support()
{
  VulkanContext ctx;
  if (!init_vulkan(&ctx, false))
    return false;

  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(ctx.physical_device, &props);
  LOG_INFO("✅ Detected Vulkan GPU: %s", props.deviceName);

  vkDestroyInstance(ctx.instance, NULL);
  return true;
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

  // Entry point
  fprintf(f, "OpEntryPoint GLCompute %%f \"main\" %%in_data %%out_data\n");

  // Decorations
  fprintf(f, "OpDecorate %%in_data DescriptorSet 0\n");
  fprintf(f, "OpDecorate %%in_data Binding 0\n");
  fprintf(f, "OpDecorate %%out_data DescriptorSet 0\n");
  fprintf(f, "OpDecorate %%out_data Binding 1\n");

  // Type declarations
  fprintf(f, "%%buf = OpTypeStruct %%int\n");
  fprintf(f, "%%buf_ptr = OpTypePointer StorageBuffer %%buf\n");

  // I/O variables
  fprintf(f, "%%in_data = OpVariable %%buf_ptr StorageBuffer\n");
  fprintf(f, "%%out_data = OpVariable %%buf_ptr StorageBuffer\n");

  // Constants commonly used
  fprintf(f, "%%int_0 = OpConstant %%int 0\n");
  fprintf(f, "%%int_1 = OpConstant %%int 1\n");

  // Function declaration
  fprintf(f, "%%f = OpFunction %%void None %%void_fn\n");
  fprintf(f, "%%entry = OpLabel\n");
}


void emit_conditional_invocation(FILE *f, const Invocation *inv)
{
  Definition *def = inv->definition;
  if (!def || !def->conditional_invocation || !def->sources)
    return;

  fprintf(f, "\n; Conditional Invocation: %s\n", def->name);
  emit_function_start(f, def->name);

  // === 1. Load Inputs from in_data buffer ===
  const SourcePlace *src = def->sources;
  char input_vars[8][32] = {0};
  int input_count = 0;

  while (src && input_count < 8)
  {
    fprintf(f, "%%idx_%d = OpConstant %%int %d\n", input_count, input_count);
    fprintf(f, "%%ptr_%d = OpAccessChain %%ptr_int %%in_data %%idx_%d\n", input_count, input_count);
    fprintf(f, "%%val_%d = OpLoad %%int %%ptr_%d\n", input_count, input_count);
    snprintf(input_vars[input_count], sizeof(input_vars[0]), "%%val_%d", input_count);
    src = src->next;
    input_count++;
  }

  if (input_count == 0)
  {
    fprintf(f, "; ⚠️ No valid input sources found\n");
    fprintf(f, "OpReturn\nOpFunctionEnd\n");
    return;
  }

  // === 2. Compute Switch Key ===
  if (input_count == 1)
  {
    fprintf(f, "%%key = %s\n", input_vars[0]);
  }
  else
  {
    fprintf(f, "%%tmp0 = OpCopyObject %%int %s\n", input_vars[0]);

    for (int i = 1; i < input_count; ++i)
    {
      fprintf(f, "%%tmp_shift_%d = OpShiftLeftLogical %%int %%tmp%d 1\n", i, i - 1);
      fprintf(f, "%%tmp%d = OpBitwiseOr %%int %%tmp_shift_%d %s\n", i, i, input_vars[i]);
    }

    fprintf(f, "%%key = %%tmp%d\n", input_count - 1);
  }

  // === 3. Emit Constants without duplication
  bool emitted_consts[256] = { false };
  ConditionalInvocationCase *c = def->conditional_invocation->cases;
  while (c)
  {
    int val = atoi(c->result);
    if (!emitted_consts[val])
    {
      fprintf(f, "%%const_%d = OpConstant %%int %d\n", val, val);
      emitted_consts[val] = true;
    }
    c = c->next;
  }

  // === 4. Switch Table
  fprintf(f, "OpSelectionMerge %%merge None\n");
  fprintf(f, "OpSwitch %%key %%default");

  c = def->conditional_invocation->cases;
  int case_idx = 0;
  while (c)
  {
    int key = (int)strtol(c->pattern, NULL, 2);
    fprintf(f, " %d %%case%d", key, case_idx);
    c = c->next;
    case_idx++;
  }
  fprintf(f, "\n");

  // === 5. Case Blocks (write to out_data[0])
  c = def->conditional_invocation->cases;
  case_idx = 0;
  while (c)
  {
    int val = atoi(c->result);
    fprintf(f, "%%case%d = OpLabel\n", case_idx);
    fprintf(f, "%%out_ptr = OpAccessChain %%ptr_int %%out_data %%int_0\n");
    fprintf(f, "OpStore %%out_ptr %%const_%d\n", val);
    fprintf(f, "OpBranch %%merge\n");
    c = c->next;
    case_idx++;
  }

  // === 6. Default & Merge
  fprintf(f, "%%default = OpLabel\n");
  fprintf(f, "OpBranch %%merge\n");
  fprintf(f, "%%merge = OpLabel\n");
  fprintf(f, "OpReturn\nOpFunctionEnd\n");
}

void emit_invocation(FILE *f, const Invocation *inv)
{
  if (!inv)
    return;

  emit_function_start(f, inv->name);

  const SourcePlace *src = inv->sources;
  int input_index = 0;
  char input_vars[2][32] = {0};
  bool emitted_consts[256] = { false };  // <-- Track emitted constants

  while (src && input_index < 2)
  {
    if (src->value) {
      int val = atoi(src->value);
      if (!emitted_consts[val]) {
        fprintf(f, "%%const_%d = OpConstant %%int %d\n", val, val);
        emitted_consts[val] = true;
      }
      snprintf(input_vars[input_index], sizeof(input_vars[0]), "%%const_%d", val);
    } else {
      fprintf(f, "%%idx_%d = OpConstant %%int %d\n", input_index, input_index);
      fprintf(f, "%%ptr_%d = OpAccessChain %%ptr_int %%in_data %%idx_%d\n", input_index, input_index);
      fprintf(f, "%%val_%d = OpLoad %%int %%ptr_%d\n", input_index, input_index);
      snprintf(input_vars[input_index], sizeof(input_vars[0]), "%%val_%d", input_index);
    }

    input_index++;
    src = src->next;
  }

  if (input_index == 2) {
    fprintf(f, "%%sum = OpIAdd %%int %s %s\n", input_vars[0], input_vars[1]);
  } else {
    LOG_WARN("⚠️ Not enough valid sources for Invocation '%s'", inv->name);
    fprintf(f, "OpReturn\nOpFunctionEnd\n");
    return;
  }

  fprintf(f, "%%out_ptr = OpAccessChain %%ptr_int %%out_data %%int_0\n");
  fprintf(f, "OpStore %%out_ptr %%sum\n");
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
    if (inv->definition && inv->definition->conditional_invocation)
      emit_conditional_invocation(f, inv);
    else
      emit_invocation(f, inv);

    fclose(f);
    LOG_INFO("✅ Wrote SPIR-V for %s to %s\n", inv->name, path);

    inv = inv->next;
  }

  return 0;
}



VkInstance create_instance()
{
  VkApplicationInfo app_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "MinimalCompute",
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
      .pEngineName = "NoEngine",
      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
      .apiVersion = VK_API_VERSION_1_0,
  };

  VkInstanceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app_info,
  };

  VkInstance instance;
  CHECK_VK(vkCreateInstance(&create_info, NULL, &instance));
  return instance;
}

VkShaderModule load_shader_module(VkDevice device, const char *path)
{
  FILE *f = fopen(path, "rb");
  if (!f)
  {
    fprintf(stderr, "❌ Cannot open %s\n", path);
    exit(1);
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);

  char *code = malloc(size);

  if ((long) fread(code, 1, size, f) != size)
  {
    fprintf(stderr, "❌ Failed to read shader code from %s\n", path);
    exit(1);
  }

  fclose(f);

  VkShaderModuleCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = size,
      .pCode = (const uint32_t *)code,
  };

  VkShaderModule shader;
  CHECK_VK(vkCreateShaderModule(device, &create_info, NULL, &shader));
  free(code);
  return shader;
}
#include <dirent.h> // For DIR, readdir
#include <unistd.h> // For access()

int create_pipeline()
{
  VulkanContext ctx;
  if (!init_vulkan(&ctx, true))
    return 1;

  DIR *dir = opendir("build/spirv_out");
  if (!dir)
  {
    fprintf(stderr, "❌ Could not open SPIR-V directory.\n");
    exit(1);
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL)
  {
    if (strstr(entry->d_name, ".spv"))
    {
      char path[512];
      snprintf(path, sizeof(path), "build/spirv_out/%s", entry->d_name);

      if (access(path, R_OK) != 0)
      {
        fprintf(stderr, "⚠️ Cannot access %s\n", path);
        continue;
      }

      VkShaderModule module = load_shader_module(ctx.device, path);
      printf("✅ Loaded SPIR-V module: %s\n", path);

      // TODO: Dispatch or attach to pipeline here

      vkDestroyShaderModule(ctx.device, module, NULL);
    }
  }

  closedir(dir);

  vkDestroyDevice(ctx.device, NULL);
  vkDestroyInstance(ctx.instance, NULL);
  return 0;
}
