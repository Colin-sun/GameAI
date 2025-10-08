// game.h
// 实现不同构建模式下的条件编译
#ifdef TRAIN_MODE
#include "utt.h"
#elif defined(INFER_MODE)
#include "utt.h"
#elif defined(TRAIN_TEST_MODE)
#include "TicTacToe.h"
#endif

// #include "TicTacToe.h"