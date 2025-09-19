add_library(core STATIC
            game.cpp
            network.cpp
            mcts.cpp)

target_include_directories(core PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                           $<INSTALL_INTERFACE:include>)

# 让 core 自己依赖 torch 等，后续 train/infer 只要 link core 即可
target_link_libraries(core PUBLIC common_deps)