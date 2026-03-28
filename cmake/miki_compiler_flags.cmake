# =============================================================================
# miki compiler flags — platform-specific warning and optimization settings
# =============================================================================

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
