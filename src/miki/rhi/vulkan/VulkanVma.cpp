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

#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wnullability-completeness"
#    pragma clang diagnostic ignored "-Wnullability-extension"
#    pragma clang diagnostic ignored "-Wunused-parameter"
#    pragma clang diagnostic ignored "-Wmissing-field-initializers"
#    pragma clang diagnostic ignored "-Wunused-variable"
#    pragma clang diagnostic ignored "-Wunused-function"
#elif defined(_MSC_VER)
#    pragma warning(push)
#    pragma warning(disable : 5105)
#endif

#include <vk_mem_alloc.h>

#if defined(__clang__)
#    pragma clang diagnostic pop
#elif defined(_MSC_VER)
#    pragma warning(pop)
#endif