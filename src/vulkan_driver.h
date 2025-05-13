#include <vulkan/vulkan.h>
bool check_spirv_gpu_support(void);
int create_pipeline(const char *spirv_dir);


typedef struct
{
  VkInstance instance;
  VkPhysicalDevice physical_device;
  VkDevice device;
} VulkanContext;
