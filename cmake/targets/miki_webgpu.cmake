# =============================================================================
# miki_webgpu — WebGPU / Dawn (Tier 3) backend
# =============================================================================

if(NOT MIKI_BUILD_WEBGPU)
    return()
endif()

add_library(miki_webgpu OBJECT
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/webgpu/WebGPUDevice.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/webgpu/WebGPUSwapchain.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/webgpu/WebGPUResources.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/webgpu/WebGPUDescriptors.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/webgpu/WebGPUPipelines.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/webgpu/WebGPUCommandBuffer.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/webgpu/WebGPUQuery.cpp
)

target_link_libraries(miki_webgpu
    PUBLIC  miki_rhi
    PUBLIC  miki::third_party::webgpu
)

if(NOT EMSCRIPTEN)
    target_link_libraries(miki_webgpu PRIVATE miki::third_party::glfw)
endif()

miki_setup_library(miki_webgpu)
