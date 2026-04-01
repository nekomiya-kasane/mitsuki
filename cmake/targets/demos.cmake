# =============================================================================
# miki demos — Example applications showcasing engine features
# =============================================================================
#
# All demos link to the unified `miki` shared library.
#

if(NOT MIKI_BUILD_EXAMPLES)
    return()
endif()

if(NOT TARGET miki)
    message(WARNING "[miki] demos require the unified miki target")
    return()
endif()

# -- Helper macro: create a demo exe linking miki ----------------------------
macro(miki_add_demo DEMO_NAME SOURCE_FILE)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${SOURCE_FILE}")
        add_executable(${DEMO_NAME} ${SOURCE_FILE})
        target_link_libraries(${DEMO_NAME} PRIVATE miki)
        miki_setup_executable(${DEMO_NAME})

        if(EMSCRIPTEN)
            set_target_properties(${DEMO_NAME} PROPERTIES
                SUFFIX ".html"
                LINK_FLAGS "--shell-file ${CMAKE_SOURCE_DIR}/demos/shell/miki_shell.html"
            )
        endif()
    endif()
endmacro()

# -- Platform demos -----------------------------------------------------------
miki_add_demo(window_manager_demo  demos/platform/window_manager_demo.cpp)
miki_add_demo(logger_demo          demos/debug/logger_demo.cpp)

# -- RHI demos ----------------------------------------------------------------
miki_add_demo(rhi_triangle_demo       demos/rhi/rhi_triangle_demo.cpp)
miki_add_demo(rhi_torus_demo          demos/rhi/rhi_torus_demo.cpp)
miki_add_demo(rhi_torus_demo_multi    demos/rhi/rhi_torus_demo_multi.cpp)
miki_add_demo(rhi_streaming_upload_demo demos/rhi/rhi_streaming_upload_demo.cpp)

# -- Third-party demos (always built if present) ------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/demos/thirdparty/CMakeLists.txt")
    add_subdirectory(demos/thirdparty)
endif()
