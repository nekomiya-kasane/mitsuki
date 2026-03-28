# =============================================================================
# miki tests — Unit and integration tests
# =============================================================================

if(NOT MIKI_BUILD_TESTS)
    return()
endif()

enable_testing()

# WindowManager tests (Mock backend)
if(TARGET miki_core AND EXISTS "${CMAKE_SOURCE_DIR}/tests/test_window_manager.cpp")
    add_executable(test_window_manager tests/test_window_manager.cpp)
    target_link_libraries(test_window_manager PRIVATE
        miki_core
        miki::third_party::gtest
        miki::third_party::gtest_main
    )
    miki_setup_executable(test_window_manager)
    add_test(NAME WindowManagerTest COMMAND test_window_manager)
endif()

# WindowManager tests (GLFW backend)
if(TARGET miki_glfw_backend AND EXISTS "${CMAKE_SOURCE_DIR}/tests/test_window_manager_glfw.cpp")
    add_executable(test_window_manager_glfw tests/test_window_manager_glfw.cpp)
    target_link_libraries(test_window_manager_glfw PRIVATE
        miki_glfw_backend
        miki::third_party::gtest
        miki::third_party::gtest_main
    )
    miki_setup_executable(test_window_manager_glfw)
    add_test(NAME WindowManagerGlfwTest COMMAND test_window_manager_glfw)
    # Mark this test as manual since it requires a display server
    set_tests_properties(WindowManagerGlfwTest PROPERTIES TIMEOUT 60)
endif()

# EventSimulator tests
if(TARGET miki_core AND EXISTS "${CMAKE_SOURCE_DIR}/tests/test_event_simulator.cpp")
    add_executable(test_event_simulator tests/test_event_simulator.cpp)
    target_link_libraries(test_event_simulator PRIVATE
        miki_core
        miki::third_party::gtest
        miki::third_party::gtest_main
    )
    miki_setup_executable(test_event_simulator)
    add_test(NAME EventSimulatorTest COMMAND test_event_simulator)
endif()

# StructuredLogger tests
if(TARGET miki_debug AND EXISTS "${CMAKE_SOURCE_DIR}/tests/test_structured_logger.cpp")
    add_executable(test_structured_logger tests/test_structured_logger.cpp)
    target_link_libraries(test_structured_logger PRIVATE
        miki_debug
        miki::third_party::gtest
        miki::third_party::gtest_main
    )
    miki_setup_executable(test_structured_logger)
    add_test(NAME StructuredLoggerTest COMMAND test_structured_logger)
endif()
