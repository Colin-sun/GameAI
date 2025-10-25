add_executable(self_play self_play.cpp)
target_link_libraries(self_play PRIVATE core)
# 启用 OpenMP
find_package(OpenMP)
if(OPENMP_FOUND)
  message(STATUS "OpenMP found")
  target_link_libraries(self_play PRIVATE OpenMP::OpenMP_CXX)
else()
  message(STATUS "OpenMP not found")
endif()

target_compile_options(self_play PRIVATE
  $<$<CONFIG:Debug>: -Wall -Wextra -g -fopenmp>
  $<$<CONFIG:Release>: -Wall -Wextra -O3 -DNDEBUG -fopenmp >)

if(ENABLE_MCTS_PURE)
  # 定义 MCTS_PURE 宏
  target_compile_definitions(self_play PUBLIC MCTS_PURE)
endif()

# 根据构建类型输出不同消息
string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_UPPER)
if(BUILD_TYPE_UPPER STREQUAL "DEBUG")
  message(STATUS "Building self_play executable in Debug mode")
elseif(BUILD_TYPE_UPPER STREQUAL "RELEASE")
  message(STATUS "Building self_play executable in Release mode")
else()
  message(WARNING "Unknown build type: ${CMAKE_BUILD_TYPE}")
endif()
