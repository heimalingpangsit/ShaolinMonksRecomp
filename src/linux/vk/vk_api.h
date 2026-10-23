#pragma once
// Vulkan entry points, loaded at run time through SDL's loader so the binary
// starts (and falls back to no picture) on a PC without libvulkan.
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#define SHAOLIN_VK_GLOBAL(X) \
    X(vkCreateInstance)      \
    X(vkEnumerateInstanceExtensionProperties)

#define SHAOLIN_VK_INSTANCE(X)                     \
    X(vkDestroyInstance)                           \
    X(vkEnumeratePhysicalDevices)                  \
    X(vkGetPhysicalDeviceProperties)               \
    X(vkGetPhysicalDeviceFeatures)                 \
    X(vkGetPhysicalDeviceFeatures2)                \
    X(vkGetPhysicalDeviceQueueFamilyProperties)    \
    X(vkGetPhysicalDeviceMemoryProperties)         \
    X(vkGetPhysicalDeviceFormatProperties)         \
    X(vkEnumerateDeviceExtensionProperties)        \
    X(vkCreateDevice)                              \
    X(vkGetDeviceProcAddr)                         \
    X(vkDestroySurfaceKHR)                         \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)        \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)   \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)        \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)

#define SHAOLIN_VK_DEVICE(X)          \
    X(vkDestroyDevice)                \
    X(vkGetDeviceQueue)               \
    X(vkDeviceWaitIdle)               \
    X(vkCreateSwapchainKHR)           \
    X(vkDestroySwapchainKHR)          \
    X(vkGetSwapchainImagesKHR)        \
    X(vkAcquireNextImageKHR)          \
    X(vkQueuePresentKHR)              \
    X(vkQueueSubmit)                  \
    X(vkCreateCommandPool)            \
    X(vkDestroyCommandPool)           \
    X(vkResetCommandPool)             \
    X(vkAllocateCommandBuffers)       \
    X(vkBeginCommandBuffer)           \
    X(vkEndCommandBuffer)             \
    X(vkCreateSemaphore)              \
    X(vkCreateFence)                  \
    X(vkDestroyFence)                 \
    X(vkWaitForFences)                \
    X(vkResetFences)                  \
    X(vkDestroySemaphore)             \
    X(vkCreateBuffer)                 \
    X(vkDestroyBuffer)                \
    X(vkGetBufferMemoryRequirements)  \
    X(vkBindBufferMemory)             \
    X(vkCreateImage)                  \
    X(vkDestroyImage)                 \
    X(vkGetImageMemoryRequirements)   \
    X(vkBindImageMemory)              \
    X(vkAllocateMemory)               \
    X(vkFreeMemory)                   \
    X(vkMapMemory)                    \
    X(vkCreateImageView)              \
    X(vkDestroyImageView)             \
    X(vkCreateSampler)                \
    X(vkDestroySampler)               \
    X(vkCreateDescriptorSetLayout)    \
    X(vkDestroyDescriptorSetLayout)   \
    X(vkCreatePipelineLayout)         \
    X(vkDestroyPipelineLayout)        \
    X(vkCreateDescriptorPool)         \
    X(vkDestroyDescriptorPool)        \
    X(vkResetDescriptorPool)          \
    X(vkAllocateDescriptorSets)       \
    X(vkUpdateDescriptorSets)         \
    X(vkCreateShaderModule)           \
    X(vkDestroyShaderModule)          \
    X(vkCreateGraphicsPipelines)      \
    X(vkDestroyPipeline)              \
    X(vkCreatePipelineCache)          \
    X(vkDestroyPipelineCache)         \
    X(vkGetPipelineCacheData)         \
    X(vkCreateQueryPool)              \
    X(vkDestroyQueryPool)             \
    X(vkGetQueryPoolResults)          \
    X(vkCmdResetQueryPool)            \
    X(vkCmdBeginQuery)                \
    X(vkCmdEndQuery)                  \
    X(vkCmdPipelineBarrier)           \
    X(vkCmdBindPipeline)              \
    X(vkCmdBindDescriptorSets)        \
    X(vkCmdBindVertexBuffers)         \
    X(vkCmdBindIndexBuffer)           \
    X(vkCmdDraw)                      \
    X(vkCmdDrawIndexed)               \
    X(vkCmdSetViewport)               \
    X(vkCmdSetScissor)                \
    X(vkCmdSetBlendConstants)         \
    X(vkCmdSetStencilReference)       \
    X(vkCmdCopyBufferToImage)         \
    X(vkCmdCopyImageToBuffer)         \
    X(vkCmdCopyImage)                 \
    X(vkCmdClearColorImage)           \
    X(vkCmdClearDepthStencilImage)    \
    X(vkCmdClearAttachments)

// core in 1.2 and 1.3, or their KHR names on older drivers
#define SHAOLIN_VK_PROMOTED(X)                   \
    X(vkWaitSemaphores, "vkWaitSemaphoresKHR")   \
    X(vkGetSemaphoreCounterValue, "vkGetSemaphoreCounterValueKHR") \
    X(vkCmdBeginRendering, "vkCmdBeginRenderingKHR") \
    X(vkCmdEndRendering, "vkCmdEndRenderingKHR")

namespace vkapi {

struct Table {
#define SHAOLIN_VK_MEMBER(name) PFN_##name name = nullptr;
#define SHAOLIN_VK_MEMBER2(name, alt) PFN_##name name = nullptr;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    SHAOLIN_VK_GLOBAL(SHAOLIN_VK_MEMBER)
    SHAOLIN_VK_INSTANCE(SHAOLIN_VK_MEMBER)
    SHAOLIN_VK_DEVICE(SHAOLIN_VK_MEMBER)
    SHAOLIN_VK_PROMOTED(SHAOLIN_VK_MEMBER2)
#undef SHAOLIN_VK_MEMBER
#undef SHAOLIN_VK_MEMBER2
};

// the process's table and device, for the objects that free themselves
extern Table vk;
extern VkDevice device;

bool LoadGlobal();                     // false without a Vulkan loader
bool LoadInstance(VkInstance instance);  // false when an entry point is missing
bool LoadDevice(VkDevice dev);

const char* ResultName(VkResult r);

}  // namespace vkapi
