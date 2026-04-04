# =============================================================================
# miki_rhi — Render Hardware Interface abstraction
# =============================================================================

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/include/miki/rhi/RhiTypes.h")
    return()
endif()

if(NOT TARGET miki_core)
    message(WARNING "[miki] miki_rhi requires miki_core")
    return()
endif()

set(MIKI_RHI_SOURCES
    src/miki/rhi/RenderSurface.cpp
    src/miki/rhi/SurfaceManager.cpp
    src/miki/rhi/PipelineCache.cpp
    src/miki/rhi/MainPipelineFactory.cpp
    src/miki/rhi/CompatPipelineFactory.cpp
    src/miki/rhi/DeviceFactory.cpp
    src/miki/rhi/RhiValidation.cpp
    src/miki/rhi/mock/MockRenderSurface.cpp
    src/miki/frame/FrameManager.cpp
    src/miki/frame/CommandPoolAllocator.cpp
    src/miki/frame/DeferredDestructor.cpp
    src/miki/frame/SyncScheduler.cpp
    src/miki/frame/AsyncTaskManager.cpp
    src/miki/frame/FrameOrchestrator.cpp
    src/miki/resource/StagingRing.cpp
    src/miki/resource/ReadbackRing.cpp
    src/miki/resource/UploadManager.cpp
)

add_library(miki_rhi OBJECT ${MIKI_RHI_SOURCES})
target_link_libraries(miki_rhi PUBLIC miki_core)
miki_setup_library(miki_rhi)

# RHI validation layer toggle.
# -DMIKI_RHI_VALIDATION_OVERRIDE=ON  -> force enable
# -DMIKI_RHI_VALIDATION_OVERRIDE=OFF -> force disable
# Not set -> header default applies (Debug=1, Release=0 via #ifndef).
if(DEFINED MIKI_RHI_VALIDATION_OVERRIDE)
    if(MIKI_RHI_VALIDATION_OVERRIDE)
        target_compile_definitions(miki_rhi PUBLIC MIKI_RHI_VALIDATION=1)
    else()
        target_compile_definitions(miki_rhi PUBLIC MIKI_RHI_VALIDATION=0)
    endif()
endif()

# Propagate backend selection as PUBLIC compile definitions so that
# Device.h / AllBackends.h can use #if MIKI_BUILD_VULKAN etc.
# Also link backend header dependencies so AllBackends.h conditional includes work.
if(MIKI_BUILD_VULKAN)
    target_compile_definitions(miki_rhi PUBLIC MIKI_BUILD_VULKAN=1)
    target_link_libraries(miki_rhi PUBLIC miki::third_party::volk)
endif()
if(MIKI_BUILD_D3D12)
    target_compile_definitions(miki_rhi PUBLIC MIKI_BUILD_D3D12=1)
    target_link_libraries(miki_rhi PUBLIC miki::third_party::directx_headers)
endif()
if(MIKI_BUILD_OPENGL)
    target_compile_definitions(miki_rhi PUBLIC MIKI_BUILD_OPENGL=1)
    target_link_libraries(miki_rhi PUBLIC miki::third_party::glad2)
endif()
if(MIKI_BUILD_WEBGPU)
    target_compile_definitions(miki_rhi PUBLIC MIKI_BUILD_WEBGPU=1)
    target_link_libraries(miki_rhi PUBLIC miki::third_party::webgpu)
endif()
