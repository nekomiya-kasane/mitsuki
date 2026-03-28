# =============================================================================
# miki Renderer — Multi-backend GPU rendering engine for CAD/CAE
# =============================================================================

cmake_minimum_required(VERSION 3.21)

project(miki
    VERSION 1.0.0
    LANGUAGES C CXX
)

# ---------------------------------------------------------------------------
# C++23 standard and compile commands export
# ---------------------------------------------------------------------------
include(cmake/miki_modules.cmake)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Project options (backends, sanitizers, testing, etc.)
# ---------------------------------------------------------------------------
include(cmake/miki_options.cmake)

# ---------------------------------------------------------------------------
# Helper functions (compiler flags, DLL copy, target setup)
# ---------------------------------------------------------------------------
include(cmake/miki_copy_runtime_dlls.cmake)
include(cmake/miki_compiler_flags.cmake)

# ---------------------------------------------------------------------------
# Dependencies (third_party handles all vendored libs)
# ---------------------------------------------------------------------------
add_subdirectory(third_party)

# ---------------------------------------------------------------------------
# Global include directories
# ---------------------------------------------------------------------------
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# Library targets (order matters: dependencies first)
# ---------------------------------------------------------------------------
include(cmake/targets/miki_core.cmake)
include(cmake/targets/miki_rhi.cmake)
include(cmake/targets/miki_vulkan.cmake)
include(cmake/targets/miki_d3d12.cmake)
include(cmake/targets/miki_glfw_backend.cmake)
include(cmake/targets/miki_debug.cmake)

# ---------------------------------------------------------------------------
# Tests and demos
# ---------------------------------------------------------------------------
include(cmake/targets/tests.cmake)
include(cmake/targets/demos.cmake)
