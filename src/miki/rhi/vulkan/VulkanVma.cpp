/** @file VulkanVma.cpp
 *  @brief Single translation unit for VMA implementation.
 *
 *  VMA is header-only. This file defines VMA_IMPLEMENTATION exactly once.
 *  volk provides function pointers; VMA must NOT link vulkan-1.lib directly.
 */

#include <volk.h>

// VMA must not call Vulkan functions directly — volk loads them at runtime.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
