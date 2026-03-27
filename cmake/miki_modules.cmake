# =============================================================================
# miki C++23 standard enforcement
# =============================================================================
#
# Included before project() to ensure the C++23 standard is set early.
# The COCA toolchain already sets C++23 via COCA_CXX_STANDARD, but we
# enforce it explicitly for clarity and portability.
#
# C++20/23 modules are intentionally disabled. The project uses traditional
# headers (.h) and implementation files (.cpp).
# =============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
