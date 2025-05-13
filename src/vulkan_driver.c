#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "eval.h"
#include "log.h"
#include <vulkan/vulkan.h>
#include <assert.h>
#include <dirent.h> // For DIR, readdir
#include <unistd.h> // For access()
#include "vulkan_driver.h"

typedef struct SPIRVOp {
    char *opcode;               // e.g., "OpTypeInt"
    char **operands;           // e.g., {"%int", "32", "1"}
    size_t operand_count;
    struct SPIRVOp *next;
} SPIRVOp;

#define CHECK_VK(result)                                                   \
  if (result != VK_SUCCESS)                                                \
  {                                                                        \
    fprintf(stderr, "❌ Vulkan error at line %d: %d\n", __LINE__, result); \
    exit(1);                                                               \
  }


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



bool check_spirv_gpu_support(void)
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

int create_pipeline(const char *spirv_dir)
{
  VulkanContext ctx;
  if (!init_vulkan(&ctx, true))
    return 1;

  DIR *dir = opendir(spirv_dir);
  if (!dir)
  {
    fprintf(stderr, "❌ Could not open SPIR-V directory: %s\n", spirv_dir);
    exit(1);
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL)
  {
    if (strstr(entry->d_name, ".spv"))
    {
      char path[512];
      snprintf(path, sizeof(path), "%s/%s", spirv_dir, entry->d_name);

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

