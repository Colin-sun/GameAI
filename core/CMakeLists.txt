if(ENABLE_TRAIN OR ENABLE_TRAIN_TEST OR ENABLE_INFER OR ENABLE_MCTS_PURE)
    set(CORE_SOURCES)

    # 根据不同模式条件编译
    if(ENABLE_MCTS_PURE)
        # 编译 utt.cpp, MCTS目录下的TreeNode.cpp, mcts_pure.cpp
        list(APPEND CORE_SOURCES
            utt.cpp
            MCTS/TreeNode.cpp
        )
        message(STATUS "Build Pure MCTS Core")
    endif()

    add_library(core STATIC ${CORE_SOURCES})

    target_include_directories(core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)

    # 根据构建类型设置优化等级和调试选项
    target_compile_options(core PUBLIC
        $<$<CONFIG:Debug>:-g -Wall -Wextra -Wpedantic>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )

    # 根据构建类型输出不同消息
    string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_UPPER)
    if(BUILD_TYPE_UPPER STREQUAL "DEBUG")
        message(STATUS "Building core library in Debug mode")
    elseif(BUILD_TYPE_UPPER STREQUAL "RELEASE")
        message(STATUS "Building core library in Release mode")
    else()
        message(WARNING "Unknown build type: ${CMAKE_BUILD_TYPE}")
    endif()

    # 让 core 自己依赖 torch 等，后续 train/infer 只要 link core 即可
    target_link_libraries(core PUBLIC common_deps)
endif()
