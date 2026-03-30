/** @file AllCommandBuffers.h
 *  @brief Conditionally includes backend command buffer definitions.
 *
 *  Any translation unit that calls CommandListHandle::Dispatch() must include this header,
 *  because Dispatch() contains static_cast to concrete backend command buffer classes.
 *
 *  See: AllBackends.h (same pattern for DeviceHandle::Dispatch).
 *  Namespace: miki::rhi
 */
#pragma once

// clang-format off
#if MIKI_BUILD_VULKAN
#    include "miki/rhi/backend/VulkanCommandBuffer.h"
#endif

#if MIKI_BUILD_D3D12
#    include "miki/rhi/backend/D3D12CommandBuffer.h"
#endif

#if MIKI_BUILD_OPENGL
#    include "miki/rhi/backend/OpenGLCommandBuffer.h"
#endif

#if MIKI_BUILD_WEBGPU
#    include "miki/rhi/backend/WebGPUCommandBuffer.h"
#endif

#include "miki/rhi/backend/MockCommandBuffer.h"
// clang-format on
