cmake_minimum_required(VERSION 3.18)
project(AIUTT)

set(CMAKE_C_COMPILER   gcc-12)
set(CMAKE_CXX_COMPILER g++-12)

# 设置默认构建类型（如果没有指定，默认为 Release）
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 选项
option(ENABLE_TRAIN "Build training executable" ON)
option(ENABLE_INFER "Build inference executable" ON)

# 统一找库
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/libtorch)
find_package(Torch REQUIRED)
# 项目也需要jsoncpp的.so 文件, 在编译时链接

# 把 torch/OpenMP/jsoncpp 打包成接口库
add_library(common_deps INTERFACE)
target_link_libraries(common_deps INTERFACE
                      ${TORCH_LIBRARIES}
                      /usr/lib/x86_64-linux-gnu/libjsoncpp.so)
target_include_directories(common_deps INTERFACE
                           ${TORCH_INCLUDE_DIRS})

# 关键：先加 core，再管子目录
add_subdirectory(core)
if(ENABLE_TRAIN)
  add_subdirectory(train)
endif()
if(ENABLE_INFER)
  add_subdirectory(infer)
endif()