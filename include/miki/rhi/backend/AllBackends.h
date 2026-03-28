/** @file AllBackends.h
 *  @brief Conditionally includes backend device definitions.
 *
 *  Any translation unit that calls DeviceHandle::Dispatch() must include this
 *  header, because Dispatch() contains static_cast to concrete backend classes.
 *  Only backends enabled via MIKI_BUILD_* compile definitions are included.
 *  Disabled backends use unreachable() in the Dispatch switch — no complete
 *  type needed, no header dependency.
 *
 *  See specs/02-rhi-design.md §2.2.
 *
 *  Namespace: miki::rhi
 */
#pragma once

#if MIKI_BUILD_VULKAN
#    include "miki/rhi/backend/VulkanDevice.h"
#    include "miki/rhi/backend/VulkanCompatDevice.h"
#endif

#if MIKI_BUILD_D3D12
#    include "miki/rhi/backend/D3D12Device.h"
#endif

#if MIKI_BUILD_OPENGL
#    include "miki/rhi/backend/OpenGLDevice.h"
#endif

#if MIKI_BUILD_WEBGPU
#    include "miki/rhi/backend/WebGPUDevice.h"
#endif
