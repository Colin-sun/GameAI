set(CORE_SOURCES
    utt.cpp
    # MCTS/TreeNode.cpp
)

# 根据不同模式条件编译
if(ENABLE_MCTS_PURE)
    message(STATUS "Build Pure MCTS Core")
endif()

add_library(core STATIC ${CORE_SOURCES})

target_include_directories(core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)

# 根据构建类型设置优化等级和调试选项
target_compile_options(core PUBLIC
    $<$<CONFIG:Debug>:-g -Wall -Wextra>
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

if(ANDROID_BUILD)
    # 如果用到 STL
    target_link_libraries(core PUBLIC stdc++)
endif()
