# =============================================================================
# miki_shader -- Slang shader compilation pipeline
# =============================================================================
#
# Provides: SlangCompiler, PermutationCache, ShaderWatcher, SlangFeatureProbe
# Depends:  miki_core, miki_rhi (for Format.h, BackendType), miki::third_party::slang
#

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/include/miki/shader/ShaderTypes.h")
    return()
endif()

if(NOT TARGET miki_core)
    message(WARNING "[miki] miki_shader requires miki_core")
    return()
endif()

if(NOT TARGET miki_rhi)
    message(WARNING "[miki] miki_shader requires miki_rhi")
    return()
endif()

if(NOT TARGET miki::third_party::slang)
    message(WARNING "[miki] miki_shader requires miki::third_party::slang (prebuilt or source)")
    return()
endif()

set(MIKI_SHADER_SOURCES
    src/miki/shader/SlangCompiler.cpp
    src/miki/shader/SlangFeatureProbe.cpp
    src/miki/shader/PermutationCache.cpp
    src/miki/shader/ShaderWatcher.cpp
)

add_library(miki_shader OBJECT ${MIKI_SHADER_SOURCES})
target_link_libraries(miki_shader
    PUBLIC  miki_core
    PUBLIC  miki_rhi
    PRIVATE miki::third_party::slang
)
miki_setup_library(miki_shader)

# Copy slang DLLs next to executables that link miki_shader
function(target_copy_slang_binaries Target)
    set(SLANG_BIN_DIR "${CMAKE_SOURCE_DIR}/third_party/slang-prebuilt/bin")
    foreach(dll slang-compiler.dll slang-rt.dll slang-glslang.dll slang-glsl-module.dll)
        if(EXISTS "${SLANG_BIN_DIR}/${dll}")
            add_custom_command(TARGET ${Target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SLANG_BIN_DIR}/${dll}" "$<TARGET_FILE_DIR:${Target}>"
                COMMENT "Copying ${dll} to $<TARGET_FILE_DIR:${Target}>")
        endif()
    endforeach()
    # Copy standard module directory
    file(GLOB SLANG_STD_MODULES "${SLANG_BIN_DIR}/slang-standard-module-*")
    foreach(stdmod_dir ${SLANG_STD_MODULES})
        get_filename_component(stdmod_name "${stdmod_dir}" NAME)
        add_custom_command(TARGET ${Target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${stdmod_dir}" "$<TARGET_FILE_DIR:${Target}>/${stdmod_name}"
            COMMENT "Copying ${stdmod_name} to $<TARGET_FILE_DIR:${Target}>")
    endforeach()
endfunction()
