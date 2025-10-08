// 实现不同构建模式下的条件编译
#ifdef TRAIN_MODE
#include "utt.cpp"
#elif defined(INFER_MODE)
#include "utt.cpp"
#elif defined(TRAIN_TEST_MODE)
#include "TicTacToe.cpp"
#endif

// #include "TicTacToe.cpp"