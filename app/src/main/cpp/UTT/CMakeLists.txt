cmake_minimum_required(VERSION 3.22.1)
project("gameai")

# 设置默认构建类型（如果没有指定，默认为 Release）
if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release)
endif()
# 设置 C++ 标准和编译警告
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ANDROID_MAX_PAGE_SIZE "16384") # 16 KB

option(ENABLE_MCTS_PURE "Build pure MCTS AI executable" OFF)
option(ANDROID_BUILD "Build for android app" OFF)
option(LINUX_BUILD "Build for linux" OFF)
option(ENABLE_BENCHMARK "Build benchmark executable (linux ONLY)" OFF)
option(ENABLE_TOOL "Build params decision tool (linux ONLY)" OFF)

# 生成core静态库
add_subdirectory(core)

if(ANDROID_BUILD)
        # 接口生成
        add_library(${CMAKE_PROJECT_NAME} SHARED
                interface/mcts_pure_bridge.cpp)
        target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
                "-Wl,-z,max-page-size=16384")

        target_link_libraries(${CMAKE_PROJECT_NAME}
                core
                android
                log)
elseif(LINUX_BUILD) # linux上运行
        add_subdirectory(humanplay)
        if(ENABLE_BENCHMARK)
                add_subdirectory(benchmark)
        endif()
        if(ENABLE_TOOL)
                add_subdirectory(tool)
        endif()
endif()
