#include <vulkan/vulkan.h>
bool check_spirv_gpu_support();
int create_pipeline();


typedef struct
{
  VkInstance instance;
  VkPhysicalDevice physical_device;
  VkDevice device;
} VulkanContext;
