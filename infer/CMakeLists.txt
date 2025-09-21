add_executable(infer gameplay.cpp)
target_link_libraries(infer PRIVATE core)
target_compile_options(infer PRIVATE
  $<$<CONFIG:Debug>:   -Wall -Wextra -g>
  $<$<CONFIG:Release>: -Wall -Wextra -O3>)