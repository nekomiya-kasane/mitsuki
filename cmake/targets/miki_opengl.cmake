# =============================================================================
# miki_opengl — OpenGL 4.3+ (Tier 4) backend
# =============================================================================

if(NOT MIKI_BUILD_OPENGL)
    return()
endif()

add_library(miki_opengl STATIC
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/opengl/OpenGLDevice.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/opengl/OpenGLSwapchain.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/opengl/OpenGLResources.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/opengl/OpenGLDescriptors.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/opengl/OpenGLPipelines.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/opengl/OpenGLCommandBuffer.cpp
    ${PROJECT_SOURCE_DIR}/src/miki/rhi/opengl/OpenGLQuery.cpp
)

target_link_libraries(miki_opengl
    PUBLIC  miki_rhi
    PUBLIC  miki::third_party::glad2
    PRIVATE miki::third_party::glfw
)

if(WIN32)
    target_link_libraries(miki_opengl PRIVATE opengl32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(miki_opengl PRIVATE GL)
elseif(APPLE)
    find_library(OPENGL_FRAMEWORK OpenGL)
    target_link_libraries(miki_opengl PRIVATE ${OPENGL_FRAMEWORK})
endif()

miki_setup_library(miki_opengl)
