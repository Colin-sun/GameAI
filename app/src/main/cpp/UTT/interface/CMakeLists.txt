add_library(interface STATIC
        mcts_pure_bridge.cpp)

target_include_directories(interface PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core)   # 需要 core 头文件

target_link_libraries(interface
        core
        android
        log)