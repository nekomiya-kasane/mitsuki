# =============================================================================
# miki_copy_runtime_dlls(TARGET)
#
# POST_BUILD: copy Slang DLLs + Dawn DLL + libc++ runtime to the target's
# output directory.  Called from demo and test CMakeLists that need these
# shared libraries at runtime.
# =============================================================================

function(miki_copy_runtime_dlls TARGET_NAME)
    if(NOT WIN32)
        return()
    endif()

    # --- Slang DLLs ---
    set(_SLANG_DLL_DIR "${PROJECT_SOURCE_DIR}/third_party/slang-prebuilt/bin")
    set(_SLANG_DLLS
        slang.dll slang-rt.dll slang-compiler.dll
        slang-glslang.dll slang-glsl-module.dll slang-llvm.dll
    )
    foreach(_dll ${_SLANG_DLLS})
        if(EXISTS "${_SLANG_DLL_DIR}/${_dll}")
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_SLANG_DLL_DIR}/${_dll}"
                    "$<TARGET_FILE_DIR:${TARGET_NAME}>/${_dll}"
                COMMENT "Copying ${_dll} -> ${TARGET_NAME}"
            )
        endif()
    endforeach()

    # --- Dawn WebGPU DLL ---
    set(_DAWN_DLL "${PROJECT_SOURCE_DIR}/third_party/webgpu/dawn/prebuilt/bin/webgpu_dawn.dll")
    if(EXISTS "${_DAWN_DLL}")
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_DAWN_DLL}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/webgpu_dawn.dll"
            COMMENT "Copying webgpu_dawn.dll -> ${TARGET_NAME}"
        )
    endif()

    # --- libc++ runtime (coca toolchain) ---
    get_filename_component(_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_cxx_dll "${_compiler_dir}/c++.dll")
    if(EXISTS "${_cxx_dll}")
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_cxx_dll}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/c++.dll"
            COMMENT "Copying c++.dll -> ${TARGET_NAME}"
        )
    endif()
endfunction()
