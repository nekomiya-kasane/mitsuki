# =============================================================================
# miki toolchain wrapper — includes COCA toolchain with project-specific fixes
# =============================================================================
#
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/miki-coca.cmake ...
# =============================================================================

if(NOT DEFINED ENV{COCA_TOOLCHAIN_CMAKE} OR "$ENV{COCA_TOOLCHAIN_CMAKE}" STREQUAL "")
    include("C:/Users/nekom/Downloads/22/toolchains/coca-toolchain/cmake/toolchain.cmake")
    message(WARNING
        "COCA toolchain is not activated: COCA_TOOLCHAIN_CMAKE is empty.\n"
        "PowerShell:  python <coca-toolchain>/setup.py | Invoke-Expression\n"
        "Then restart Cursor (or open a new terminal) and run CMake: Configure again.")
else()
    include("$ENV{COCA_TOOLCHAIN_CMAKE}")
endif()

# Include the real COCA toolchain

# wasm-emscripten profile: the COCA toolchain already delegates to
# Emscripten.cmake. Skip all MSVC/Windows-specific fixups.
if(DEFINED COCA_TARGET_PROFILE AND COCA_TARGET_PROFILE STREQUAL "wasm-emscripten")
    return()
endif()

# Fix archiver for win-x64-clang profile: COCA sets CMAKE_AR to llvm-lib
# (MSVC-style) but the GNU driver causes CMake to emit ar-style flags (qc).
# llvm-ar handles both COFF and ar-style flags correctly.
if(CMAKE_CXX_COMPILER MATCHES "clang\\+\\+")
    set(CMAKE_AR "$ENV{COCA_TOOLCHAIN_BIN}/llvm-ar${CMAKE_EXECUTABLE_SUFFIX}"
        CACHE FILEPATH "" FORCE)
endif()

# Add WinRT SDK include path — COCA toolchain includes um/ucrt/shared but
# not winrt. D3D12 backend needs wrl/client.h (Windows Runtime C++ Template
# Library) which lives under the winrt subdirectory.
if(WIN32 AND DEFINED COCA_TOOLCHAIN_ROOT)
    set(_winrt_dir "${COCA_TOOLCHAIN_ROOT}/sysroots/x86_64-windows-msvc/sdk/Include/10.0.26100.0/winrt")
    if(EXISTS "${_winrt_dir}")
        include_directories(SYSTEM "${_winrt_dir}")
    endif()
endif()

# Force release CRT (/MD → VCRUNTIME140.dll) for ALL build types.
# Rationale: prebuilt third-party DLLs (Slang, etc.) ship release-only.
# Mixing debug CRT (VCRUNTIME140D.dll) with release CRT in the same process
# causes heap corruption (0xC0000374). Clang+MSVC-target doesn't benefit
# from debug CRT; debug info is controlled by -g / -gcodeview instead.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "" FORCE)
