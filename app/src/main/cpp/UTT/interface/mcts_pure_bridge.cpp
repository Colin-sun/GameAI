// mcts_pure_bridge.cpp
// JNI Cpp侧接口
#include <jni.h>
#include <stdint.h>
#include "MCTS/mcts_pure.h"
#include "utt.h"

// 创建游戏状态对象
// 输入：row-major 一维 int[] board, int next_board_row, int next_board_col (零坐标，没有则均为-1)
extern "C" {
JNIEXPORT jlong
JNICALL
Java_com_aiprojects_gameai_NativeUTT_nCreate(JNIEnv* env, jclass, jintArray board_,     // 扁平 int[]
                                                jint nextBoardRow,
                                                jint nextBoardCol) {
    /* 1. 直接访问 Java 数组元素，无需拷贝 */
    jint* src = env->GetIntArrayElements(board_, nullptr);

    /* 2. 还原二维棋盘 */
    std::array<std::array<int, BOARD_SIZE>, BOARD_SIZE> board{};
    for (int i = 0; i < BOARD_SIZE; ++i){
        for (int j = 0; j < BOARD_SIZE; ++j){
            board[i][j] = src[i * BOARD_SIZE + j];
        }
    }

    env->ReleaseIntArrayElements(board_, src, JNI_ABORT); // 销毁本地缓冲区

    /* 3. 构造 C++ 对象 */
    NewGameParameters params;
    params.board      = board;
    params.next_board = std::make_pair(nextBoardRow, nextBoardCol);
    return reinterpret_cast<jlong>(new UltimateTicTacToe(params));
}
}

// 创建空盘
extern "C" {
    JNIEXPORT jlong
    JNICALL
    Java_com_aiprojects_gameai_NativeUTT_nCreateEmpty(JNIEnv*, jclass) {
        return reinterpret_cast<jlong>(new UltimateTicTacToe());
    }
}

// 更新游戏状态 (走一步子)
extern "C" {
JNIEXPORT void
JNICALL
Java_com_aiprojects_gameai_NativeUTT_nMakeMove(JNIEnv*, jclass, jlong game_ptr, jint row,
                                                  jint col) {
    auto* game = reinterpret_cast<UltimateTicTacToe*>(game_ptr);
    game->make_move(game->get_action_index(row, col));
}
}

// 检查当前游戏是否结束：0未结束，1玩家X获胜，2玩家O获胜，-1平局
extern "C"
JNIEXPORT jint JNICALL
Java_com_aiprojects_gameai_NativeUTT_nGetDoneWiner(JNIEnv *, jclass, jlong game_ptr) {
    auto* game = reinterpret_cast<UltimateTicTacToe*>(game_ptr);
    return game->get_done_winner().second;
}

// 销毁游戏状态对象
extern "C" {
JNIEXPORT void
JNICALL
Java_com_aiprojects_gameai_NativeUTT_nDestroy(JNIEnv *, jclass, jlong game_ptr) {
    delete reinterpret_cast<UltimateTicTacToe*>(game_ptr);
}
}

// 创建 MCTS 对象
extern "C" {
JNIEXPORT jlong
JNICALL
Java_com_aiprojects_gameai_NativeMCTSPure_nCreate(JNIEnv *, jclass, jint n_playout, jfloat c_puct) {
    std::random_device rd;
    void* mctspure_cpp = new MCTSPure<UltimateTicTacToe>(n_playout, c_puct, std::mt19937(rd()));
    return reinterpret_cast<jlong>(mctspure_cpp);
}
}

// 返回最佳动作的 action_index
extern "C" {
JNIEXPORT jint
JNICALL
Java_com_aiprojects_gameai_NativeMCTSPure_nGetBestMoveIndex(JNIEnv *, jclass, jlong mctspure_ptr, jlong game_ptr) {
    auto* mctspure = reinterpret_cast<MCTSPure<UltimateTicTacToe>*>(mctspure_ptr);
    auto* current_game = reinterpret_cast<UltimateTicTacToe*>(game_ptr);
    return mctspure->get_move(*current_game);
}
}

// 销毁 MCTS 对象, 传入指针
extern "C" {
JNIEXPORT void
JNICALL
Java_com_aiprojects_gameai_NativeMCTSPure_nDestroy(JNIEnv * , jclass, jlong mctspure_ptr ) {
    delete reinterpret_cast <MCTSPure<UltimateTicTacToe>*> ( mctspure_ptr ) ;
}
}

