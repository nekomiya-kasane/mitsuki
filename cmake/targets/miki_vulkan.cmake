# =============================================================================
# miki_vulkan — Vulkan 1.4 (Tier 1) backend
# =============================================================================

if(NOT MIKI_BUILD_VULKAN)
    return()
endif()

add_library(miki_vulkan STATIC
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/vulkan/VulkanDevice.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/vulkan/VulkanSwapchain.cpp
)

target_link_libraries(miki_vulkan
    PUBLIC  miki_rhi
    PUBLIC  miki::third_party::volk
    PRIVATE miki::third_party::vma
)

miki_setup_library(miki_vulkan)
