# 统一查找 OpenMP，避免重复
find_package(OpenMP REQUIRED)

# 定义一个函数用于添加训练相关可执行文件
function(add_train_executable target_name source_file)
  add_executable(${target_name} ${source_file})
  target_link_libraries(${target_name} PRIVATE core OpenMP::OpenMP_CXX)
  # 根据构建类型设置优化等级和调试选项
  target_compile_options(${target_name} PRIVATE
    $<$<CONFIG:Debug>:-g -Wall -Wextra -Wpedantic>
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
  )
endfunction()

if(ENABLE_TRAIN)
  add_train_executable(train train.cpp)
  message(STATUS "Build train executable")
endif()

if(ENABLE_TRAIN_TEST)
  add_train_executable(train_test train_test.cpp)
  message(STATUS "Build train_test executable")
endif()

# 根据构建类型输出不同消息
string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_UPPER)
if(BUILD_TYPE_UPPER STREQUAL "DEBUG")
  message(STATUS "Building train executable in Debug mode")
elseif(BUILD_TYPE_UPPER STREQUAL "RELEASE")
  message(STATUS "Building train executable in Release mode")
else()
  message(WARNING "Unknown build type: ${CMAKE_BUILD_TYPE}")
endif()
