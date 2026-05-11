add_executable(benchmark speed_test.cpp)
target_link_libraries(benchmark PRIVATE core)

target_compile_options(benchmark PRIVATE
  $<$<CONFIG:Debug>: -Wall -Wextra -g>
  $<$<CONFIG:Release>: -Wall -Wextra -O3 -DNDEBUG>)

if(ENABLE_MCTS_PURE)
  # 定义 MCTS_PURE 宏
  target_compile_definitions(benchmark PUBLIC MCTS_PURE)
  find_package(jsoncpp REQUIRED)
  target_link_libraries(benchmark PRIVATE jsoncpp_lib)
endif()

# 根据构建类型输出不同消息
string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_UPPER)
if(BUILD_TYPE_UPPER STREQUAL "DEBUG")
  message(STATUS "Building benchmark executable in Debug mode")
elseif(BUILD_TYPE_UPPER STREQUAL "RELEASE")
  message(STATUS "Building benchmark executable in Release mode")
else()
  message(WARNING "Unknown build type: ${CMAKE_BUILD_TYPE}")
endif()
