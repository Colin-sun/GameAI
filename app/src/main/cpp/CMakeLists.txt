cmake_minimum_required(VERSION 3.22.1)
project("gameai")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ANDROID_MAX_PAGE_SIZE "16384")

get_filename_component(GAMEAI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)

set(ANDROID_BUILD ON CACHE BOOL "Build for android app" FORCE)

add_subdirectory("${GAMEAI_ROOT}/native" "${CMAKE_CURRENT_BINARY_DIR}/native")

add_library(${CMAKE_PROJECT_NAME} SHARED
        android/mcts_pure_bridge.cpp)

target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
        "-Wl,-z,max-page-size=16384")

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
        core
        android
        log)
