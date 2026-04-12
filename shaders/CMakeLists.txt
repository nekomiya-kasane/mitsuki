# shaders/CMakeLists.txt — Precompiled Slang module generation (§15.8.1)
#
# Compiles shader library modules to .slang-module binary IR at build time.
# Pass shaders link against these precompiled modules at runtime for ~4.8x speedup.
# DEPENDS tracks primary + all implementing files so edits trigger correct rebuilds.

# Find slangc executable
if(MIKI_SLANG_PREBUILT)
    set(SLANGC_EXECUTABLE "${CMAKE_SOURCE_DIR}/third_party/slang-prebuilt/bin/slangc${CMAKE_EXECUTABLE_SUFFIX}")
else()
    # Source build — slangc target should exist
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

# Output directory for precompiled modules
set(MIKI_PRECOMPILED_DIR "${CMAKE_BINARY_DIR}/shaders/precompiled")
file(MAKE_DIRECTORY "${MIKI_PRECOMPILED_DIR}")

# All 13 shader library modules (Phases 1b through 17+).
# Each module is precompiled to .slang-module for pass-level linking.
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
    # miki-core -> miki_core (for output filename)
    string(REPLACE "-" "_" MOD_UNDERSCORED ${MOD})
    # miki-core -> core (for implementing file directory)
    string(REGEX REPLACE "^miki-" "" MOD_SHORT ${MOD})

    # Collect ALL source files for this module (primary + implementing files)
    # so CMake rebuilds the precompiled module when any constituent changes.
    file(GLOB_RECURSE _MOD_SOURCES
        "${CMAKE_SOURCE_DIR}/shaders/miki/${MOD}.slang"
        "${CMAKE_SOURCE_DIR}/shaders/miki/${MOD_SHORT}/*.slang"
    )

    set(_OUTPUT "${MIKI_PRECOMPILED_DIR}/${MOD_UNDERSCORED}.slang-module")

    add_custom_command(
        OUTPUT "${_OUTPUT}"
        COMMAND "${SLANGC_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/shaders/miki/${MOD}.slang"
            -o "${_OUTPUT}"
            -I "${CMAKE_SOURCE_DIR}/shaders/miki"
        DEPENDS ${_MOD_SOURCES}
        COMMENT "[miki] Precompiling Slang module: ${MOD} (${MOD_SHORT}/*.slang)"
        VERBATIM
    )

    list(APPEND MIKI_PRECOMPILED_MODULES "${_OUTPUT}")
endforeach()

add_custom_target(miki_shader_modules ALL
    DEPENDS ${MIKI_PRECOMPILED_MODULES}
)

# Export the precompiled module directory for use by tests and runtime
set(MIKI_PRECOMPILED_MODULE_DIR "${MIKI_PRECOMPILED_DIR}" CACHE PATH
    "Directory containing precompiled .slang-module files" FORCE)

message(STATUS "[miki] Shader precompilation: ${CMAKE_CURRENT_LIST_FILE}")
message(STATUS "[miki]   Modules: ${MIKI_SHADER_MODULES}")
message(STATUS "[miki]   Output:  ${MIKI_PRECOMPILED_DIR}")
