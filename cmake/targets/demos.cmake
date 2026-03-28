# =============================================================================
# miki demos — Example applications showcasing engine features
# =============================================================================

if(NOT MIKI_BUILD_EXAMPLES)
    return()
endif()

# ---------------------------------------------------------------------------
# WindowManager Demo — Multi-window, events, parent-child hierarchy
# ---------------------------------------------------------------------------
if(TARGET miki_glfw_backend AND TARGET miki_debug AND EXISTS "${CMAKE_SOURCE_DIR}/demos/platform/window_manager_demo.cpp")
    add_executable(window_manager_demo demos/platform/window_manager_demo.cpp)
    target_link_libraries(window_manager_demo PRIVATE miki_glfw_backend miki_debug)
    miki_setup_executable(window_manager_demo)

    if(EMSCRIPTEN)
        # Generate HTML shell for browser execution
        # Note: USE_WEBGPU is deprecated, use --use-port=emdawnwebgpu for WebGPU support
        set_target_properties(window_manager_demo PROPERTIES
            SUFFIX ".html"
            LINK_FLAGS "--shell-file ${CMAKE_SOURCE_DIR}/demos/shell/miki_shell.html"
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Logger Demo — Structured logging demonstration
# ---------------------------------------------------------------------------
if(TARGET miki_debug AND EXISTS "${CMAKE_SOURCE_DIR}/demos/debug/logger_demo.cpp")
    add_executable(logger_demo demos/debug/logger_demo.cpp)
    target_link_libraries(logger_demo PRIVATE miki_debug)
    miki_setup_executable(logger_demo)
endif()

# ---------------------------------------------------------------------------
# RHI Triangle Demo — renders a color triangle on WebGPU or OpenGL backend
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/demos/rhi/rhi_triangle_demo.cpp")
    set(_rhi_triangle_deps "")
    if(TARGET miki_webgpu)
        list(APPEND _rhi_triangle_deps miki_webgpu)
    endif()
    if(TARGET miki_opengl)
        list(APPEND _rhi_triangle_deps miki_opengl)
    endif()

    if(_rhi_triangle_deps)
        add_executable(rhi_triangle_demo demos/rhi/rhi_triangle_demo.cpp)
        target_link_libraries(rhi_triangle_demo PRIVATE ${_rhi_triangle_deps} miki_rhi miki_debug glfw)
        miki_setup_executable(rhi_triangle_demo)

        if(EMSCRIPTEN)
            set_target_properties(rhi_triangle_demo PROPERTIES
                SUFFIX ".html"
                LINK_FLAGS "--shell-file ${CMAKE_SOURCE_DIR}/demos/shell/miki_shell.html"
            )
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Third-party demos (always built if present)
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/demos/thirdparty/CMakeLists.txt")
    add_subdirectory(demos/thirdparty)
endif()
