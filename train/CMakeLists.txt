add_executable(train train.cpp)

# 只在编 train 时检查 OpenMP
find_package(OpenMP REQUIRED)
target_link_libraries(train PRIVATE core OpenMP::OpenMP_CXX)

# 只需要 link core，torch 等依赖会被 core 自动带进来
target_link_libraries(train PRIVATE core)

# 编译选项照旧
target_compile_options(train PRIVATE
  $<$<CONFIG:Debug>:   -Wall -Wextra -g>
  $<$<CONFIG:Release>: -Wall -Wextra -O3>)