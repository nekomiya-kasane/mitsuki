# =============================================================================
# miki compiler flags — platform-specific warning and optimization settings
# =============================================================================

# ---------------------------------------------------------------------------
# Debug build: comprehensive debug information for debuggers
# ---------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Common flags for all platforms
        add_compile_options(
            -fstandalone-debug           # Emit full debug info for all types (not just used ones)
            -fno-limit-debug-info        # Don't limit debug info size
            -fno-omit-frame-pointer      # Keep frame pointers for stack traces
            -fno-optimize-sibling-calls  # Don't optimize tail calls (better stack traces)
            -O0                          # No optimization (predictable debugging)
        )
        if(WIN32)
            # Windows Clang: use CodeView format (LLDB on Windows reads CodeView/PDB)
            add_compile_options(
                -g                       # Enable debug info
                -gcodeview               # CodeView format for Windows debuggers
                -gcolumn-info            # Include column numbers
            )
            add_link_options(-Wl,/DEBUG:FULL)
        else()
            # Unix/macOS: use DWARF format
            add_compile_options(
                -g3                      # Maximum debug info (includes macro definitions)
                -glldb                   # Tune debug info for LLDB
                -gdwarf-5                # DWARF 5 format
                -gcolumn-info            # Include column numbers
            )
        endif()
    elseif(MSVC)
        add_compile_options(
            /Zi                          # Full debug info in PDB
            /Od                          # No optimization
            /Oy-                         # Keep frame pointers
            /DEBUG:FULL                  # Full debug info in linker
        )
        add_link_options(/DEBUG:FULL)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(
            -g3                          # Maximum debug info
            -ggdb3                       # Tune for GDB
            -fno-omit-frame-pointer      # Keep frame pointers
            -fno-optimize-sibling-calls  # Better stack traces
            -gdwarf-5                    # DWARF 5 format
            -O0                          # No optimization
        )
    endif()
endif()

# Helper function to apply standard warning flags to a target
function(miki_target_warnings TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4)
    elseif(NOT EMSCRIPTEN)
        target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(MIKI_CI)
        if(MSVC)
            target_compile_options(${TARGET_NAME} PRIVATE /WX)
        else()
            target_compile_options(${TARGET_NAME} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Helper function to set up a miki library with standard settings
function(miki_setup_library TARGET_NAME)
    target_compile_features(${TARGET_NAME} PUBLIC cxx_std_23)
    target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include)
    miki_target_warnings(${TARGET_NAME})
endfunction()

# Helper function to set up a miki executable with standard settings
function(miki_setup_executable TARGET_NAME)
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_23)
    miki_target_warnings(${TARGET_NAME})
    if(WIN32 AND NOT EMSCRIPTEN)
        miki_copy_runtime_dlls(${TARGET_NAME})
    endif()
endfunction()
