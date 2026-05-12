cmake_minimum_required(VERSION 3.22.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project("gameai")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_MCTS_PURE "Build pure MCTS AI executable" OFF)
option(ANDROID_BUILD "Build for android app" OFF)
option(LINUX_BUILD "Build for linux" OFF)
option(ENABLE_BENCHMARK "Build benchmark executable (linux ONLY)" OFF)
option(ENABLE_TOOL "Build params decision tool (linux ONLY)" OFF)
option(ENABLE_PYTHON_BINDINGS "Build Python bindings (linux ONLY)" OFF)

add_subdirectory(core)

if(LINUX_BUILD)
    if(ENABLE_BENCHMARK)
        add_subdirectory(benchmark)
    endif()

    if(ENABLE_TOOL)
        add_subdirectory(tools)
    endif()

    if(ENABLE_PYTHON_BINDINGS)
        add_subdirectory(python)
    endif()
endif()
