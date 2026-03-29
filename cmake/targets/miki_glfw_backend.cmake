# =============================================================================
# miki_glfw_backend — GLFW-based IWindowBackend implementation
# =============================================================================

if(NOT MIKI_BUILD_GLFW_BACKEND)
    return()
endif()

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/miki/platform/glfw/GlfwWindowBackend.cpp")
    return()
endif()

if(NOT TARGET miki_core)
    message(WARNING "[miki] miki_glfw_backend requires miki_core")
    return()
endif()

add_library(miki_glfw_backend OBJECT
    src/miki/platform/glfw/GlfwWindowBackend.cpp
    src/miki/platform/glfw/GlfwEventSimulator.cpp
)

# GlfwWindowBackend only needs miki_core (RhiTypes.h is header-only, no link dependency)
target_link_libraries(miki_glfw_backend PUBLIC miki_core)
miki_setup_library(miki_glfw_backend)

if(NOT EMSCRIPTEN)
    target_link_libraries(miki_glfw_backend PUBLIC miki::third_party::glfw)
endif()
