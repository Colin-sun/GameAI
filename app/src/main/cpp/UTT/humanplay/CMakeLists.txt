add_executable(gameplay gameplay.cpp)
target_link_libraries(gameplay PRIVATE core)

target_compile_options(gameplay PRIVATE
  $<$<CONFIG:Debug>: -Wall -Wextra -g>
  $<$<CONFIG:Release>: -Wall -Wextra -O3 -DNDEBUG>)

if(ENABLE_MCTS_PURE AND LINUX_BUILD)
  # 定义MCTS_PURE宏
  target_compile_definitions(gameplay PUBLIC MCTS_PURE)
  find_package(jsoncpp REQUIRED)
  target_link_libraries(gameplay PRIVATE jsoncpp_lib)
else()
  message(WARNING "invalid build Platform")
endif()

# 根据构建类型输出不同消息
string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_UPPER)
if(BUILD_TYPE_UPPER STREQUAL "DEBUG")
  message(STATUS "Building gameplay executable in Debug mode")
elseif(BUILD_TYPE_UPPER STREQUAL "RELEASE")
  message(STATUS "Building gameplay executable in Release mode")
else()
  message(WARNING "Unknown build type: ${CMAKE_BUILD_TYPE}")
endif()
