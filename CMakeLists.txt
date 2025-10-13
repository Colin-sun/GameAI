# 添加对 ENABLE_TRAIN_TEST 的支持
cmake_minimum_required(VERSION 3.18)
project(AIUTT)

set(CMAKE_C_COMPILER gcc-12)
set(CMAKE_CXX_COMPILER g++-12)

# 设置默认构建类型（如果没有指定，默认为 Release）
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 设置 C++ 标准和编译警告
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 选项
# option(ENABLE_TRAIN "Build training executable" OFF)
# option(ENABLE_INFER "Build infer executable" OFF)
# option(ENABLE_TRAIN_TEST "Build train_test executable" OFF)
option(ENABLE_MCTS_PURE "Build pure MCTS AI executable" OFF)

# # 统一找库
# set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/libtorch)
# find_package(Torch REQUIRED)
# # 项目也需要jsoncpp的.so 文件, 在编译时链接

# # 把 torch/OpenMP/jsoncpp 打包成接口库
# add_library(common_deps INTERFACE)
# target_link_libraries(common_deps INTERFACE
#   ${TORCH_LIBRARIES}
#   /usr/lib/x86_64-linux-gnu/libjsoncpp.so)
# target_include_directories(common_deps INTERFACE
#   ${TORCH_INCLUDE_DIRS})

# 把 torch/OpenMP/jsoncpp 打包成接口库
add_library(common_deps INTERFACE)
target_link_libraries(common_deps INTERFACE
  /usr/lib/x86_64-linux-gnu/libjsoncpp.so)

# 先加 core，再管子目录
add_subdirectory(core)

# 根据选项添加子目录
if(ENABLE_MCTS_PURE)
  add_subdirectory(humanplay)
endif()
