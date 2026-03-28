# =============================================================================
# miki project options
# =============================================================================

# Backend selection
if(EMSCRIPTEN)
    set(MIKI_BUILD_VULKAN OFF CACHE BOOL "" FORCE)
    set(MIKI_BUILD_D3D12  OFF CACHE BOOL "" FORCE)
    set(MIKI_BUILD_OPENGL OFF CACHE BOOL "" FORCE)
    set(MIKI_BUILD_WEBGPU ON  CACHE BOOL "" FORCE)
    set(MIKI_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
else()
    option(MIKI_BUILD_VULKAN "Build Vulkan Tier1 backend"          ON)
    option(MIKI_BUILD_D3D12  "Build D3D12 backend (Windows-only)"  ON)
    option(MIKI_BUILD_OPENGL "Build OpenGL 4.3+ Tier4 backend"     ON)
    option(MIKI_BUILD_WEBGPU "Build WebGPU Tier3 backend (Dawn)"   ON)
endif()

# Demo backend
set(MIKI_DEMO_BACKEND "glfw" CACHE STRING "Demo windowing backend: glfw | neko")
set_property(CACHE MIKI_DEMO_BACKEND PROPERTY STRINGS "glfw" "neko")

# Testing
option(MIKI_BUILD_TESTS  "Build unit and integration tests"    ON)

# CI mode (stricter warnings)
option(MIKI_CI           "Enable CI mode (warnings as errors)" OFF)

# Sanitizers (orthogonal to COCA presets — these add miki-specific flags)
option(MIKI_ENABLE_ASAN  "Enable AddressSanitizer"             OFF)
option(MIKI_ENABLE_TSAN  "Enable ThreadSanitizer"              OFF)
option(MIKI_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer"   OFF)

# Apply sanitizer flags
if(MIKI_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
if(MIKI_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()
if(MIKI_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

# Kernel options
option(MIKI_KERNEL_OCCT "Build OcctKernel with OpenCASCADE Technology" OFF)

# Validate: D3D12 only on Windows
if(MIKI_BUILD_D3D12 AND NOT WIN32)
    message(FATAL_ERROR "[miki] MIKI_BUILD_D3D12=ON requires Windows")
endif()
