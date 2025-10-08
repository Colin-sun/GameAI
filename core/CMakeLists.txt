if(ENABLE_TRAIN OR ENABLE_TRAIN_TEST OR ENABLE_INFER)
    set(CORE_SOURCES
        game.cpp
        network.cpp
        mcts.cpp)

    if(ENABLE_INFER)
        list(APPEND CORE_SOURCES minimax.cpp)
    endif()

    add_library(core STATIC ${CORE_SOURCES})

    target_include_directories(core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)

    # 根据不同模式定义预处理器宏
    if(ENABLE_TRAIN)
        target_compile_definitions(core PUBLIC TRAIN_MODE)
        message(STATUS "Build UTT Core")
    endif()

    if(ENABLE_TRAIN_TEST)
        target_compile_definitions(core PUBLIC TRAIN_TEST_MODE)
        message(STATUS "Build TicTacToe Core")
    endif()

    if(ENABLE_INFER)
        target_compile_definitions(core PUBLIC INFER_MODE)
        message(STATUS "Build UTT & minimax Core")
    endif()

    # 让 core 自己依赖 torch 等，后续 train/infer 只要 link core 即可
    target_link_libraries(core PUBLIC common_deps)
endif()
