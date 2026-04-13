# shaders/CMakeLists.txt — Precompiled Slang module generation (§15.8.1)
#
# Compiles shader library modules to .slang-module binary IR at build time.
# Pass shaders link against these precompiled modules at runtime for ~4.8x speedup.
#
# Dependency tracking strategy (CMake 4.0+ / Ninja):
#   - DEPENDS:  GLOB_RECURSE of primary + implementing files (bootstrap for first build)
#   - DEPFILE:  slangc -depfile outputs precise import-graph deps (cross-module, incremental)
#   - CODEGEN:  Enables `cmake --build . --target codegen` for shader-only iteration

cmake_policy(SET CMP0171 NEW)  # Reserve 'codegen' as builtin target (3.31+)

# ---------------------------------------------------------------------------
# Locate slangc
# ---------------------------------------------------------------------------
if(MIKI_SLANG_PREBUILT)
    set(SLANGC_EXECUTABLE "${CMAKE_SOURCE_DIR}/third_party/slang-prebuilt/bin/slangc${CMAKE_EXECUTABLE_SUFFIX}")
else()
    if(TARGET slangc)
        set(SLANGC_EXECUTABLE "$<TARGET_FILE:slangc>")
    else()
        set(SLANGC_EXECUTABLE "${CMAKE_SOURCE_DIR}/third_party/slang-prebuilt/bin/slangc${CMAKE_EXECUTABLE_SUFFIX}")
    endif()
endif()

if(NOT EXISTS "${SLANGC_EXECUTABLE}" AND NOT TARGET slangc)
    message(WARNING "[miki] slangc not found at ${SLANGC_EXECUTABLE}, skipping shader precompilation")
    return()
endif()

# ---------------------------------------------------------------------------
# Output directories
# ---------------------------------------------------------------------------
set(MIKI_PRECOMPILED_DIR "${CMAKE_BINARY_DIR}/shaders/precompiled")
set(MIKI_DEPFILE_DIR     "${CMAKE_BINARY_DIR}/shaders/depfiles")
file(MAKE_DIRECTORY "${MIKI_PRECOMPILED_DIR}")
file(MAKE_DIRECTORY "${MIKI_DEPFILE_DIR}")

# ---------------------------------------------------------------------------
# All 13 shader library modules (Phases 1b through 17+)
# ---------------------------------------------------------------------------
set(MIKI_SHADER_MODULES
    miki-core
    miki-math
    miki-brdf
    miki-geometry
    miki-lighting
    miki-shadow
    miki-postfx
    miki-debug
    miki-cad
    miki-cae
    miki-rt
    miki-xr
    miki-neural
)

set(MIKI_PRECOMPILED_MODULES "")

foreach(MOD ${MIKI_SHADER_MODULES})
    # miki-core -> miki_core (output filename)
    string(REPLACE "-" "_" MOD_UNDERSCORED ${MOD})
    # miki-core -> core (implementing file subdirectory)
    string(REGEX REPLACE "^miki-" "" MOD_SHORT ${MOD})

    set(_PRIMARY "${CMAKE_SOURCE_DIR}/shaders/miki/${MOD}.slang")
    set(_OUTPUT  "${MIKI_PRECOMPILED_DIR}/${MOD_UNDERSCORED}.slang-module")
    set(_DEPFILE "${MIKI_DEPFILE_DIR}/${MOD_UNDERSCORED}.d")

    # Bootstrap DEPENDS: GLOB_RECURSE catches primary + implementing files.
    # After first build, DEPFILE provides precise cross-module import tracking.
    file(GLOB_RECURSE _MOD_SOURCES
        "${_PRIMARY}"
        "${CMAKE_SOURCE_DIR}/shaders/miki/${MOD_SHORT}/*.slang"
    )

    add_custom_command(
        OUTPUT  "${_OUTPUT}"
        COMMAND "${SLANGC_EXECUTABLE}"
            "${_PRIMARY}"
            -o "${_OUTPUT}"
            -I "${CMAKE_SOURCE_DIR}/shaders/miki"
            -depfile "${_DEPFILE}"
        DEPENDS   ${_MOD_SOURCES}
        DEPFILE   "${_DEPFILE}"
        CODEGEN
        COMMENT   "[miki] Precompiling Slang module: ${MOD}"
        VERBATIM
    )

    list(APPEND MIKI_PRECOMPILED_MODULES "${_OUTPUT}")
endforeach()

# ---------------------------------------------------------------------------
# Aggregate target (ALL ensures modules are built with the main build)
# ---------------------------------------------------------------------------
add_custom_target(miki_shader_modules ALL
    DEPENDS ${MIKI_PRECOMPILED_MODULES}
)

# Export for tests and runtime
set(MIKI_PRECOMPILED_MODULE_DIR "${MIKI_PRECOMPILED_DIR}" CACHE PATH
    "Directory containing precompiled .slang-module files" FORCE)

message(STATUS "[miki] Shader precompilation: ${CMAKE_CURRENT_LIST_FILE}")
message(STATUS "[miki]   Modules: ${MIKI_SHADER_MODULES}")
message(STATUS "[miki]   Output:  ${MIKI_PRECOMPILED_DIR}")
message(STATUS "[miki]   Depfiles: ${MIKI_DEPFILE_DIR}")
message(STATUS "[miki]   Use 'cmake --build . --target codegen' for shader-only builds")
