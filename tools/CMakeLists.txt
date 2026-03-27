# =============================================================================
# Build-time tools (native only, never cross-compiled)
# =============================================================================

if(NOT EMSCRIPTEN)
    add_executable(precompile_wgsl precompile_wgsl.cpp)

    target_link_libraries(precompile_wgsl PRIVATE
        miki::shader
        miki::core)

    set(MIKI_RENDER_SHADER_DIR "${PROJECT_SOURCE_DIR}/shaders/render")
    set(MIKI_COMMON_SHADER_DIR "${PROJECT_SOURCE_DIR}/shaders/common")
    cmake_path(CONVERT "${MIKI_RENDER_SHADER_DIR}" TO_CMAKE_PATH_LIST MIKI_RENDER_SHADER_DIR_FWD)
    cmake_path(CONVERT "${MIKI_COMMON_SHADER_DIR}" TO_CMAKE_PATH_LIST MIKI_COMMON_SHADER_DIR_FWD)
    target_compile_definitions(precompile_wgsl PRIVATE
        "MIKI_RENDER_SHADER_DIR_LITERAL=\"${MIKI_RENDER_SHADER_DIR_FWD}\""
        "MIKI_COMMON_SHADER_DIR_LITERAL=\"${MIKI_COMMON_SHADER_DIR_FWD}\""
    )
endif()
