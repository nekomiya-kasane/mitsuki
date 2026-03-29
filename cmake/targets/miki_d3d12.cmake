# =============================================================================
# miki_d3d12 — Direct3D 12 (Tier 1) backend
# =============================================================================

if(NOT MIKI_BUILD_D3D12 OR NOT WIN32)
    return()
endif()

add_library(miki_d3d12 STATIC
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/d3d12/D3D12Device.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/d3d12/D3D12Swapchain.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/d3d12/D3D12Resources.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/d3d12/D3D12Descriptors.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/d3d12/D3D12Pipelines.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/d3d12/D3D12CommandBuffer.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/d3d12/D3D12Query.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/d3d12/D3D12AccelStruct.cpp
)

if(MSVC)
    target_compile_options(miki_d3d12 PRIVATE /wd4201)  # ignore anonymous struct warning
else()
    target_compile_options(miki_d3d12 PRIVATE -Wno-language-extension-token)
endif()

target_link_libraries(miki_d3d12
    PUBLIC  miki_rhi
    PUBLIC  miki::third_party::directx_headers
    PRIVATE miki::third_party::d3d12ma
    PRIVATE d3d12 dxgi dxguid
)

miki_setup_library(miki_d3d12)
