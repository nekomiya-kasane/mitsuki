# =============================================================================
# miki_copy_runtime_dlls(TARGET_NAME)
#
# POST_BUILD rule that copies every DLL needed at runtime next to the
# executable, so demos / tests can be run in-place from the build tree.
#
# Three DLL sources are handled (in order):
#
#   1. TARGET_RUNTIME_DLLS  (CMake 3.21+)
#      Automatically resolves all SHARED-library dependencies that the
#      target links against (transitively).  This covers icetea.dll,
#      tapioca.dll, and any future SHARED libs added to the link graph.
#
#   2. Prebuilt vendor DLLs  (Slang, Dawn, etc.)
#      IMPORTED targets whose DLLs are not in the link graph because they
#      are loaded at runtime or via IMPORTED_LOCATION.  We glob them from
#      known vendor directories so new DLLs are picked up automatically.
#
#   3. Toolchain runtime  (libc++ c++.dll from COCA clang)
#      The COCA toolchain ships its own libc++; executables need it.
#
# Usage:
#   include(cmake/miki_copy_runtime_dlls)   # once, in root CMakeLists.txt
#   miki_copy_runtime_dlls(my_exe_target)   # per executable target
# =============================================================================

function(miki_copy_runtime_dlls TARGET_NAME)
    if(NOT WIN32)
        return()
    endif()

    # ── 1. Linked SHARED libraries (automatic via CMake 3.21+) ────────────
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND "$<$<BOOL:$<TARGET_RUNTIME_DLLS:${TARGET_NAME}>>:${CMAKE_COMMAND}>"
            "$<$<BOOL:$<TARGET_RUNTIME_DLLS:${TARGET_NAME}>>:-E>"
            "$<$<BOOL:$<TARGET_RUNTIME_DLLS:${TARGET_NAME}>>:copy_if_different>"
            "$<$<BOOL:$<TARGET_RUNTIME_DLLS:${TARGET_NAME}>>:$<TARGET_RUNTIME_DLLS:${TARGET_NAME}>>"
            "$<$<BOOL:$<TARGET_RUNTIME_DLLS:${TARGET_NAME}>>:$<TARGET_FILE_DIR:${TARGET_NAME}>>"
        COMMAND_EXPAND_LISTS
        COMMENT "[miki] Copying linked runtime DLLs -> ${TARGET_NAME}"
    )

    # ── 2. Prebuilt vendor DLLs (Slang, Dawn, …) ─────────────────────────
    set(_VENDOR_DLL_DIRS
        "${PROJECT_SOURCE_DIR}/third_party/slang-prebuilt/bin"
        "${PROJECT_SOURCE_DIR}/third_party/webgpu/dawn/prebuilt/bin"
    )
    foreach(_dir ${_VENDOR_DLL_DIRS})
        if(IS_DIRECTORY "${_dir}")
            file(GLOB _vendor_dlls "${_dir}/*.dll")
            foreach(_dll ${_vendor_dlls})
                get_filename_component(_name "${_dll}" NAME)
                add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_dll}"
                        "$<TARGET_FILE_DIR:${TARGET_NAME}>/${_name}"
                    COMMENT "[miki] Copying ${_name} -> ${TARGET_NAME}"
                )
            endforeach()
        endif()
    endforeach()

    # ── 3. Toolchain runtime (libc++ c++.dll from COCA clang) ─────────────
    get_filename_component(_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_cxx_dll "${_compiler_dir}/c++.dll")
    if(EXISTS "${_cxx_dll}")
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_cxx_dll}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/c++.dll"
            COMMENT "[miki] Copying c++.dll -> ${TARGET_NAME}"
        )
    endif()
endfunction()
