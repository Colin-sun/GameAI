// cpp 库接口
package com.aiprojects.gameai

/**
 * 一次性加载 native 库
 */
object NativeLib {
    init {
        System.loadLibrary("gameai")
    }
}

/**
 * 对应 cpp 中 UltimateTicTacToe 的三个函数
 */
object NativeUTT {
    init {
        NativeLib   // 触发 loadLibrary
    }

    @JvmStatic
    external fun nCreate(
        board_: IntArray,        // 扁平 row-major 棋盘
        nextBoardRow: Int,      // 下一个子棋盘行号，-1 表示无
        nextBoardCol: Int
    ): Long                    // 返回 C++ 对象的地址

    @JvmStatic
    external fun nCreateEmpty(): Long

    @JvmStatic
    external fun nMakeMove(
        game_ptr: Long,          // 由 nCreate 返回的指针
        row: Int,
        col: Int
    )

    @JvmStatic
    external fun nDestroy(game_ptr: Long)
}

/**
 * 对应 cpp 中 NativeMCTSPure 的三个函数
 */
object NativeMCTSPure {
    init {
        NativeLib
    }

    @JvmStatic
    external fun nCreate(
        n_playout: Int,          // MCTS 模拟次数
        c_puct: Float            // PUCT 常数
    ): Long

    @JvmStatic
    external fun nGetBestMoveIndex(
        mctspure_ptr: Long,
        game_ptr: Long
    ): Int                     // 返回最佳动作的 action_index

    @JvmStatic
    external fun nDestroy(mctspure_ptr: Long)
}


