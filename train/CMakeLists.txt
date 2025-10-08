# 统一查找 OpenMP，避免重复
find_package(OpenMP REQUIRED)

# 定义一个函数用于添加训练相关可执行文件
function(add_train_executable target_name source_file)
  add_executable(${target_name} ${source_file})
  target_link_libraries(${target_name} PRIVATE core OpenMP::OpenMP_CXX)
  target_compile_options(${target_name} PRIVATE
    $<$<CONFIG:Debug>: -Wall -Wextra -g>
    $<$<CONFIG:Release>: -Wall -Wextra -O3>)
endfunction()

if(ENABLE_TRAIN)
  add_train_executable(train train.cpp)
  message(STATUS "Build train executable")
endif()

if(ENABLE_TRAIN_TEST)
  add_train_executable(train_test train_test.cpp)
  message(STATUS "Build train_test executable")
endif()
