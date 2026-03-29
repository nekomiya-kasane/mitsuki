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

# WindowManager spec tests (GLFW backend — all SS13 test cases)
if(TARGET miki_glfw_backend AND EXISTS "${CMAKE_SOURCE_DIR}/tests/test_window_manager_spec.cpp")
    add_executable(test_window_manager_spec tests/test_window_manager_spec.cpp)
    target_link_libraries(test_window_manager_spec PRIVATE
        miki_glfw_backend
        miki::third_party::gtest
        miki::third_party::gtest_main
    )
    miki_setup_executable(test_window_manager_spec)
    add_test(NAME WindowManagerSpecTest COMMAND test_window_manager_spec)
    set_tests_properties(WindowManagerSpecTest PROPERTIES TIMEOUT 120)
endif()

# SurfaceManager + FrameManager integration tests (all real GPU backends)
if(TARGET miki_glfw_backend AND TARGET miki_rhi AND EXISTS "${CMAKE_SOURCE_DIR}/tests/test_surface_integration.cpp")
    set(_surface_test_deps miki_glfw_backend miki_rhi miki_debug)
    if(TARGET miki_webgpu)
        list(APPEND _surface_test_deps miki_webgpu)
    endif()
    if(TARGET miki_opengl)
        list(APPEND _surface_test_deps miki_opengl)
    endif()
    if(TARGET miki_vulkan)
        list(APPEND _surface_test_deps miki_vulkan)
    endif()
    if(TARGET miki_d3d12)
        list(APPEND _surface_test_deps miki_d3d12)
    endif()

    add_executable(test_surface_integration tests/test_surface_integration.cpp)
    target_link_libraries(test_surface_integration PRIVATE
        ${_surface_test_deps}
        miki::third_party::gtest
        miki::third_party::gtest_main
    )
    miki_setup_executable(test_surface_integration)
    add_test(NAME SurfaceIntegrationTest COMMAND test_surface_integration)
    set_tests_properties(SurfaceIntegrationTest PROPERTIES TIMEOUT 300)
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

# =============================================================================
# RHI tests — §3-§13 spec coverage
# =============================================================================

# Collect all available backend targets for RHI tests
set(_rhi_test_deps miki_rhi)
if(TARGET miki_vulkan)
    list(APPEND _rhi_test_deps miki_vulkan)
endif()
if(TARGET miki_d3d12)
    list(APPEND _rhi_test_deps miki_d3d12)
endif()
if(TARGET miki_opengl)
    list(APPEND _rhi_test_deps miki_opengl)
endif()
if(TARGET miki_webgpu)
    list(APPEND _rhi_test_deps miki_webgpu)
endif()

# §3 Handle System (pure CPU — no device needed)
if(TARGET miki_rhi AND EXISTS "${CMAKE_SOURCE_DIR}/tests/rhi/test_rhi_handle.cpp")
    add_executable(test_rhi_handle tests/rhi/test_rhi_handle.cpp)
    target_link_libraries(test_rhi_handle PRIVATE
        miki_rhi
        miki::third_party::gtest
        miki::third_party::gtest_main
    )
    target_include_directories(test_rhi_handle PRIVATE ${CMAKE_SOURCE_DIR}/tests/rhi)
    miki_setup_executable(test_rhi_handle)
    add_test(NAME RhiHandleTest COMMAND test_rhi_handle)
endif()

# Helper macro for parameterized RHI tests (require real GPU backends)
macro(miki_add_rhi_test TEST_NAME SOURCE_FILE)
    if(TARGET miki_rhi AND EXISTS "${CMAKE_SOURCE_DIR}/${SOURCE_FILE}")
        add_executable(${TEST_NAME} ${SOURCE_FILE})
        target_link_libraries(${TEST_NAME} PRIVATE
            ${_rhi_test_deps}
            miki::third_party::gtest
            miki::third_party::gtest_main
        )
        target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/tests/rhi)
        miki_setup_executable(${TEST_NAME})
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
        set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 120)
    endif()
endmacro()

# §4 Device & Capability
miki_add_rhi_test(test_rhi_device       tests/rhi/test_rhi_device.cpp)
# §5 Resources
miki_add_rhi_test(test_rhi_resource     tests/rhi/test_rhi_resource.cpp)
# §6 Descriptors & Bindings
miki_add_rhi_test(test_rhi_descriptor   tests/rhi/test_rhi_descriptor.cpp)
# §7 Command Buffers
miki_add_rhi_test(test_rhi_command_buffer tests/rhi/test_rhi_command_buffer.cpp)
# §8 Pipelines
miki_add_rhi_test(test_rhi_pipeline     tests/rhi/test_rhi_pipeline.cpp)
# §9 Synchronization
miki_add_rhi_test(test_rhi_sync         tests/rhi/test_rhi_sync.cpp)
# §10 Acceleration Structures (T1 only)
miki_add_rhi_test(test_rhi_accel_struct tests/rhi/test_rhi_accel_struct.cpp)
# §11 Swapchain (pure CPU struct tests)
if(TARGET miki_rhi AND EXISTS "${CMAKE_SOURCE_DIR}/tests/rhi/test_rhi_swapchain.cpp")
    add_executable(test_rhi_swapchain tests/rhi/test_rhi_swapchain.cpp)
    target_link_libraries(test_rhi_swapchain PRIVATE
        miki_rhi
        miki::third_party::gtest
        miki::third_party::gtest_main
    )
    target_include_directories(test_rhi_swapchain PRIVATE ${CMAKE_SOURCE_DIR}/tests/rhi)
    miki_setup_executable(test_rhi_swapchain)
    add_test(NAME RhiSwapchainTest COMMAND test_rhi_swapchain)
endif()
# §12 Query Pools
miki_add_rhi_test(test_rhi_query        tests/rhi/test_rhi_query.cpp)
# Integration tests (multi-subsystem call sequences)
miki_add_rhi_test(test_rhi_integration  tests/rhi/test_rhi_integration.cpp)
set_tests_properties(test_rhi_integration PROPERTIES TIMEOUT 300)
