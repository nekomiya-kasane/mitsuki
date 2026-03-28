# =============================================================================
# miki_core — Platform abstraction layer (WindowManager, Event system)
# =============================================================================

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/miki/platform/WindowManager.cpp")
    return()
endif()

set(MIKI_CORE_SOURCES
    src/miki/platform/WindowManager.cpp
    src/miki/platform/EventSimulator.cpp
)

set(MIKI_CORE_HEADERS
    include/miki/platform/WindowManager.h
    include/miki/platform/NativeWindowHandle.h
    include/miki/platform/Event.h
    include/miki/platform/EventSimulator.h
    include/miki/core/ChunkedSlotMap.h
    include/miki/core/Flags.h
    include/miki/core/Result.h
    include/miki/core/ErrorCode.h
    include/miki/core/Types.h
)

add_library(miki_core STATIC ${MIKI_CORE_SOURCES} ${MIKI_CORE_HEADERS})
miki_setup_library(miki_core)

if(EMSCRIPTEN)
    # contrib.glfw3 port provides GLFW3 on Emscripten (pongasoft/emscripten-glfw)
    target_compile_options(miki_core PUBLIC "--use-port=contrib.glfw3")
    target_link_options(miki_core PUBLIC "--use-port=contrib.glfw3")
else()
    target_link_libraries(miki_core PUBLIC miki::third_party::glfw)
endif()
