find_package(Python COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(gameai_native bindings.cpp)

target_link_libraries(gameai_native PRIVATE core)
target_include_directories(gameai_native PRIVATE
    ${CMAKE_SOURCE_DIR}/games
    ${CMAKE_SOURCE_DIR}/mcts
)

set_target_properties(gameai_native PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
)
