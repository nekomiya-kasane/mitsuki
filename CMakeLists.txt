# =============================================================================
# miki Renderer — Multi-backend GPU rendering engine for CAD/CAE
# =============================================================================

cmake_minimum_required(VERSION 3.21)

project(miki
    VERSION 1.0.0
    LANGUAGES C CXX
)

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(MIKI_BUILD_TESTS "Build tests" ON)
option(MIKI_BUILD_EXAMPLES "Build example applications" ON)
option(MIKI_BUILD_VULKAN "Build Vulkan backend" OFF)
option(MIKI_BUILD_D3D12 "Build D3D12 backend" OFF)
option(MIKI_BUILD_WEBGPU "Build WebGPU backend" OFF)

# ---------------------------------------------------------------------------
# Runtime DLL copy helper (Win32 only, uses CMake 3.21+ TARGET_RUNTIME_DLLS)
# ---------------------------------------------------------------------------
include(cmake/miki_copy_runtime_dlls.cmake)

# ---------------------------------------------------------------------------
# Dependencies (third_party handles all vendored libs)
# ---------------------------------------------------------------------------
add_subdirectory(third_party)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ---------------------------------------------------------------------------
# miki engine libraries (only built when source files are present)
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/miki/platform/WindowManager.cpp")

    set(CORE_SOURCES  src/miki/platform/WindowManager.cpp)
    set(CORE_HEADERS
        include/miki/platform/WindowManager.h
        include/miki/platform/Event.h
        include/miki/core/Result.h
        include/miki/core/ErrorCode.h
        include/miki/core/Types.h
    )

    add_library(miki_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
    target_link_libraries(miki_core PUBLIC miki::third_party::glfw)
    target_compile_features(miki_core PUBLIC cxx_std_23)

    if(MSVC)
        target_compile_options(miki_core PRIVATE /W4)
    else()
        target_compile_options(miki_core PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    # RHI library (requires RhiTypes.h etc.)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/miki/rhi/RhiTypes.h")
        set(RHI_SOURCES
            src/miki/rhi/RenderSurface.cpp
            src/miki/rhi/mock/MockRenderSurface.cpp)
        add_library(miki_rhi STATIC ${RHI_SOURCES})
        target_link_libraries(miki_rhi PUBLIC miki_core)
        target_compile_features(miki_rhi PUBLIC cxx_std_23)

        # GLFW Backend
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demos/framework/glfw/GlfwWindowBackend.cpp")
            add_library(miki_glfw_backend STATIC
                demos/framework/glfw/GlfwWindowBackend.cpp)
            target_link_libraries(miki_glfw_backend
                PUBLIC miki_core miki_rhi miki::third_party::glfw)
            target_compile_features(miki_glfw_backend PUBLIC cxx_std_23)
            if(MSVC)
                target_compile_options(miki_glfw_backend PRIVATE /W4)
            else()
                target_compile_options(miki_glfw_backend PRIVATE -Wall -Wextra -Wpedantic)
            endif()

            # Example application
            if(MIKI_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/window_manager_example.cpp")
                add_executable(window_manager_example examples/window_manager_example.cpp)
                target_link_libraries(window_manager_example PRIVATE miki_glfw_backend)
                target_compile_features(window_manager_example PRIVATE cxx_std_23)
                miki_copy_runtime_dlls(window_manager_example)
            endif()
        endif()
    endif()

    # Tests
    if(MIKI_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_window_manager.cpp")
        enable_testing()
        add_executable(test_window_manager tests/test_window_manager.cpp)
        target_link_libraries(test_window_manager
            PRIVATE miki_core miki::third_party::gtest miki::third_party::gtest_main)
        target_compile_features(test_window_manager PRIVATE cxx_std_23)
        miki_copy_runtime_dlls(test_window_manager)
        add_test(NAME WindowManagerTest COMMAND test_window_manager)
    endif()
endif()

# ---------------------------------------------------------------------------
# miki_debug — Structured logging & debug infrastructure (Layer 1)
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/miki/debug/StructuredLogger.cpp")
    add_library(miki_debug STATIC
        src/miki/debug/StructuredLogger.cpp
        src/miki/debug/CrashHandler.cpp)
    target_include_directories(miki_debug
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(miki_debug PUBLIC cxx_std_23)
    target_link_libraries(miki_debug PUBLIC miki::third_party::tapioca)

    if(MSVC)
        target_compile_options(miki_debug PRIVATE /W4)
    else()
        target_compile_options(miki_debug PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()

    # Tests
    if(MIKI_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_structured_logger.cpp")
        if(NOT TARGET gtest)
            enable_testing()
        endif()
        add_executable(test_structured_logger
            tests/test_structured_logger.cpp)
        target_link_libraries(test_structured_logger
            PRIVATE miki_debug
                    miki::third_party::gtest
                    miki::third_party::gtest_main)
        target_compile_features(test_structured_logger
            PRIVATE cxx_std_23)
        miki_copy_runtime_dlls(test_structured_logger)
        add_test(NAME StructuredLoggerTest
                 COMMAND test_structured_logger)
    endif()

    # Demo
    if(MIKI_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demos/debug/logger_demo.cpp")
        add_executable(logger_demo
            demos/debug/logger_demo.cpp)
        target_link_libraries(logger_demo
            PRIVATE miki_debug)
        target_compile_features(logger_demo
            PRIVATE cxx_std_23)
        miki_copy_runtime_dlls(logger_demo)
    endif()
endif()

# ---------------------------------------------------------------------------
# Third-party demos (always built)
# ---------------------------------------------------------------------------
add_subdirectory(demos/thirdparty)
