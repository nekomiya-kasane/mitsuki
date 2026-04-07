# =============================================================================
# miki — Unified shared library (DLL) aggregating all miki_xxx OBJECT libs
# =============================================================================
#
# All tests and demos link ONLY to this target.
# Each miki_xxx OBJECT lib contributes its .obj files here.
# Third-party link dependencies are declared once on this target.
#

# -- Collect OBJECT libraries --------------------------------------------------
set(_miki_objects
    $<TARGET_OBJECTS:miki_core>
    $<TARGET_OBJECTS:miki_rhi>
    $<TARGET_OBJECTS:miki_debug>
)

if(TARGET miki_glfw_backend)
    list(APPEND _miki_objects $<TARGET_OBJECTS:miki_glfw_backend>)
endif()
if(TARGET miki_vulkan)
    list(APPEND _miki_objects $<TARGET_OBJECTS:miki_vulkan>)
endif()
if(TARGET miki_d3d12)
    list(APPEND _miki_objects $<TARGET_OBJECTS:miki_d3d12>)
endif()
if(TARGET miki_opengl)
    list(APPEND _miki_objects $<TARGET_OBJECTS:miki_opengl>)
endif()
if(TARGET miki_webgpu)
    list(APPEND _miki_objects $<TARGET_OBJECTS:miki_webgpu>)
endif()
if(TARGET miki_shader)
    list(APPEND _miki_objects $<TARGET_OBJECTS:miki_shader>)
endif()
if(TARGET miki_rendergraph)
    list(APPEND _miki_objects $<TARGET_OBJECTS:miki_rendergraph>)
endif()

# -- Create STATIC library -----------------------------------------------------
add_library(miki STATIC ${_miki_objects})
set_target_properties(miki PROPERTIES
    OUTPUT_NAME "miki"
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)
miki_setup_library(miki)

# -- Propagate compile definitions from miki_rhi (backend flags) ---------------
if(MIKI_BUILD_VULKAN)
    target_compile_definitions(miki PUBLIC MIKI_BUILD_VULKAN=1)
endif()
if(MIKI_BUILD_D3D12)
    target_compile_definitions(miki PUBLIC MIKI_BUILD_D3D12=1)
endif()
if(MIKI_BUILD_OPENGL)
    target_compile_definitions(miki PUBLIC MIKI_BUILD_OPENGL=1)
endif()
if(MIKI_BUILD_WEBGPU)
    target_compile_definitions(miki PUBLIC MIKI_BUILD_WEBGPU=1)
endif()

# -- Third-party dependencies (PUBLIC = headers needed by consumers) -----------

# Core deps
if(NOT EMSCRIPTEN)
    target_link_libraries(miki PUBLIC miki::third_party::glfw)
else()
    target_compile_options(miki PUBLIC "--use-port=contrib.glfw3")
    target_link_options(miki PUBLIC "--use-port=contrib.glfw3")
endif()

# Debug deps
if(TARGET miki::third_party::tapioca)
    target_link_libraries(miki PUBLIC miki::third_party::tapioca)
endif()

# RHI backend header deps (PUBLIC for consumer includes)
if(MIKI_BUILD_VULKAN)
    target_link_libraries(miki PUBLIC miki::third_party::volk)
endif()
if(MIKI_BUILD_D3D12)
    target_link_libraries(miki PUBLIC miki::third_party::directx_headers)
endif()
if(MIKI_BUILD_OPENGL)
    target_link_libraries(miki PUBLIC miki::third_party::glad2)
endif()
if(MIKI_BUILD_WEBGPU)
    target_link_libraries(miki PUBLIC miki::third_party::webgpu)
endif()

# RHI backend PRIVATE link deps (only needed for symbol resolution in DLL)
if(MIKI_BUILD_VULKAN AND TARGET miki::third_party::vma)
    target_link_libraries(miki PRIVATE miki::third_party::vma)
endif()
if(MIKI_BUILD_D3D12)
    if(TARGET miki::third_party::d3d12ma)
        target_link_libraries(miki PRIVATE miki::third_party::d3d12ma)
    endif()
    target_link_libraries(miki PRIVATE d3d12 dxgi dxguid)
endif()
if(MIKI_BUILD_OPENGL)
    if(WIN32)
        target_link_libraries(miki PRIVATE opengl32)
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(miki PRIVATE GL)
    elseif(APPLE)
        find_library(OPENGL_FRAMEWORK OpenGL)
        target_link_libraries(miki PRIVATE ${OPENGL_FRAMEWORK})
    endif()
endif()

# Shader deps
if(TARGET miki_shader AND TARGET miki::third_party::slang)
    target_link_libraries(miki PRIVATE miki::third_party::slang)
endif()
