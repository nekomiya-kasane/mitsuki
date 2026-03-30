# =============================================================================
# miki tests — Unit and integration tests
# =============================================================================
#
# All tests link to the unified `miki` shared library.
#

if(NOT MIKI_BUILD_TESTS)
    return()
endif()

if(NOT TARGET miki)
    message(WARNING "[miki] tests require the unified miki target")
    return()
endif()

enable_testing()

# -- Helper macro: create a test exe linking miki + gtest ---------------------
macro(miki_add_test TEST_NAME SOURCE_FILE)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${SOURCE_FILE}")
        add_executable(${TEST_NAME} ${SOURCE_FILE})
        target_link_libraries(${TEST_NAME} PRIVATE
            miki
            miki::third_party::gtest
            miki::third_party::gtest_main
        )
        miki_setup_executable(${TEST_NAME})
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endif()
endmacro()

# -- Platform tests -----------------------------------------------------------
miki_add_test(test_window_manager      tests/test_window_manager.cpp)
miki_add_test(test_window_manager_glfw tests/test_window_manager_glfw.cpp)
miki_add_test(test_event_simulator     tests/test_event_simulator.cpp)
miki_add_test(test_window_manager_spec tests/test_window_manager_spec.cpp)
miki_add_test(test_structured_logger   tests/test_structured_logger.cpp)

if(TARGET test_window_manager_glfw)
    set_tests_properties(test_window_manager_glfw PROPERTIES TIMEOUT 60)
endif()
if(TARGET test_window_manager_spec)
    set_tests_properties(test_window_manager_spec PROPERTIES TIMEOUT 120)
endif()

# -- Surface integration tests ------------------------------------------------
miki_add_test(test_surface_integration tests/test_surface_integration.cpp)
if(TARGET test_surface_integration)
    set_tests_properties(test_surface_integration PROPERTIES TIMEOUT 300)
endif()

# -- RHI tests (§3-§13 spec coverage) ----------------------------------------

# §3 Handle System
miki_add_test(test_rhi_handle         tests/rhi/test_rhi_handle.cpp)
if(TARGET test_rhi_handle)
    target_include_directories(test_rhi_handle PRIVATE ${CMAKE_SOURCE_DIR}/tests/rhi)
endif()

# Helper macro for RHI tests with include dir
macro(miki_add_rhi_test TEST_NAME SOURCE_FILE)
    miki_add_test(${TEST_NAME} ${SOURCE_FILE})
    if(TARGET ${TEST_NAME})
        target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/tests/rhi)
        set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 120)
    endif()
endmacro()

# §4 Device & Capability
miki_add_rhi_test(test_rhi_device          tests/rhi/test_rhi_device.cpp)
# §5 Resources
miki_add_rhi_test(test_rhi_resource        tests/rhi/test_rhi_resource.cpp)
# §6 Descriptors & Bindings
miki_add_rhi_test(test_rhi_descriptor      tests/rhi/test_rhi_descriptor.cpp)
# §7 Command Buffers
miki_add_rhi_test(test_rhi_command_buffer  tests/rhi/test_rhi_command_buffer.cpp)
# §8 Pipelines
miki_add_rhi_test(test_rhi_pipeline        tests/rhi/test_rhi_pipeline.cpp)
# §9 Synchronization
miki_add_rhi_test(test_rhi_sync            tests/rhi/test_rhi_sync.cpp)
# §10 Acceleration Structures
miki_add_rhi_test(test_rhi_accel_struct    tests/rhi/test_rhi_accel_struct.cpp)
# §11 Swapchain
miki_add_rhi_test(test_rhi_swapchain       tests/rhi/test_rhi_swapchain.cpp)
# §12 Query Pools
miki_add_rhi_test(test_rhi_query           tests/rhi/test_rhi_query.cpp)
# Integration tests
miki_add_rhi_test(test_rhi_integration     tests/rhi/test_rhi_integration.cpp)
if(TARGET test_rhi_integration)
    set_tests_properties(test_rhi_integration PROPERTIES TIMEOUT 300)
endif()

# -- Frame tests (§17 FrameManager / SyncScheduler / DeferredDestructor) ------
miki_add_test(test_frame_manager tests/frame/test_frame_manager.cpp)
if(TARGET test_frame_manager)
    target_include_directories(test_frame_manager PRIVATE ${CMAKE_SOURCE_DIR}/tests/rhi)
    set_tests_properties(test_frame_manager PROPERTIES TIMEOUT 300)
endif()
