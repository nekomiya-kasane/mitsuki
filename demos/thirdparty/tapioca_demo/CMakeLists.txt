# tapioca_demo — Terminal rendering demo using tapioca
add_executable(tapioca_demo main.cpp)
target_link_libraries(tapioca_demo PRIVATE tapioca)
set_target_properties(tapioca_demo PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
miki_copy_runtime_dlls(tapioca_demo)
