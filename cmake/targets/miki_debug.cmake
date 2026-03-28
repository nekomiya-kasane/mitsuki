# =============================================================================
# miki_debug — Structured logging & debug infrastructure (Layer 1)
# =============================================================================

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/miki/debug/StructuredLogger.cpp")
    return()
endif()

add_library(miki_debug STATIC
    src/miki/debug/StructuredLogger.cpp
    src/miki/debug/CrashHandler.cpp
    src/miki/debug/CpuProfiler.cpp
)

miki_setup_library(miki_debug)
target_link_libraries(miki_debug PUBLIC miki::third_party::tapioca)
