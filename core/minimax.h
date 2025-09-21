// 借助带剪枝的 minimax 算法和模型 value 评估函数实现 AI 玩家
#include <iostream>
#include <vector>
#include <algorithm>
#include <limits>
#include <utility>
#include <cmath>
#include "game.h"
#include "network.hpp"

class AIPlayer {
public:
    AIPlayer(int player_id, int max_depth, ValueCNN& model);
    std::pair<int, int> get_best_move(UltimateTicTacToe& board);
private:
    int max_depth; // 最大搜索深度
    int player_id; // 1 or 2, AI 玩家编号
    ValueCNN model; // 评估函数模型
    float minimax(UltimateTicTacToe& board, int depth, bool is_maximizing, float alpha, float beta);
    float evaluate(UltimateTicTacToe& board);
    void undo_move(UltimateTicTacToe& game, std::pair<int, int> move); // 撤销走子，只能撤销最近走的一步
};