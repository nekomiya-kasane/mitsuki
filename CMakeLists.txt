# =============================================================================
# miki Window Management — GLFW-based window creation and management
# =============================================================================

cmake_minimum_required(VERSION 3.20)

project(miki_windows
    VERSION 1.0.0
    LANGUAGES CXX
)

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(MIKI_BUILD_TESTS "Build tests" ON)
option(MIKI_BUILD_EXAMPLES "Build example applications" ON)

# ---------------------------------------------------------------------------
# GLFW Configuration
# ---------------------------------------------------------------------------
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
add_subdirectory(third_party/glfw)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/include
)

# ---------------------------------------------------------------------------
# Core library sources (platform only)
# ---------------------------------------------------------------------------
set(CORE_SOURCES
    src/miki/platform/WindowManager.cpp
)

set(CORE_HEADERS
    include/miki/platform/WindowManager.h
    include/miki/platform/Event.h
    include/miki/core/Result.h
    include/miki/core/ErrorCode.h
    include/miki/core/Types.h
)

# ---------------------------------------------------------------------------
# GLFW Backend sources
# ---------------------------------------------------------------------------
set(GLFW_BACKEND_SOURCES
    demos/framework/glfw/GlfwWindowBackend.cpp
)

set(GLFW_BACKEND_HEADERS
    demos/framework/glfw/GlfwWindowBackend.h
    include/miki/rhi/RhiTypes.h
    include/miki/rhi/RenderSurface.h
)

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(miki_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_link_libraries(miki_core
    PUBLIC glfw
)

target_compile_features(miki_core PUBLIC cxx_std_23)

# ---------------------------------------------------------------------------
# GLFW Backend library
# ---------------------------------------------------------------------------
add_library(miki_glfw_backend STATIC ${GLFW_BACKEND_SOURCES} ${GLFW_BACKEND_HEADERS})
target_link_libraries(miki_glfw_backend
    PUBLIC miki_core
    PUBLIC glfw
)

target_compile_features(miki_glfw_backend PUBLIC cxx_std_23)

# ---------------------------------------------------------------------------
# Example application
# ---------------------------------------------------------------------------
if(MIKI_BUILD_EXAMPLES)
    add_executable(window_manager_example)
    target_sources(window_manager_example PRIVATE
        examples/window_manager_example.cpp
    )
    target_link_libraries(window_manager_example
        PRIVATE miki_glfw_backend
    )
    target_compile_features(window_manager_example PRIVATE cxx_std_23)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MIKI_BUILD_TESTS)
    # Only enable tests if GTest is found
    find_package(GTest QUIET)
    if(GTest_FOUND)
        enable_testing()
        
        add_executable(test_window_manager)
        target_sources(test_window_manager PRIVATE
            tests/test_window_manager.cpp
        )
        target_link_libraries(test_window_manager
            PRIVATE miki_core
            PRIVATE GTest::gtest
            PRIVATE GTest::gtest_main
        )
        target_compile_features(test_window_manager PRIVATE cxx_std_23)
        
        add_test(NAME WindowManagerTest COMMAND test_window_manager)
    else()
        message(WARNING "GTest not found. Tests will not be built.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Compiler-specific settings
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(miki_core PRIVATE /W4)
    target_compile_options(miki_glfw_backend PRIVATE /W4)
else()
    target_compile_options(miki_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(miki_glfw_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()
