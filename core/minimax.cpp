// 借助带剪枝的 minimax 算法和模型 value 评估函数实现 AI 玩家
#include "minimax.h"

AIPlayer::AIPlayer(int player_id, int max_depth, ValueCNN& model) :
    player_id(player_id),
    max_depth(max_depth),
    model(model) {
}

// 评估函数: 0 为平局，负值为反方胜，正值为本方胜
// 同时鼓励 ai在更少的步数内获胜，或拖延更多步数失败
float AIPlayer::evaluate(UltimateTicTacToe& board) {
    int winner = board.get_winner();
    int step = board.get_step();
    if (winner == 0) {
        // 游戏未结束，使用模型评估
        auto x = board_to_tensor(board);
        float current_value = model->calc_value(x);
        if (board.get_current_player() != player_id) {
            current_value = -current_value; // 轮到对方，价值取反
        }
        return current_value;
    } else if (winner == 3 - player_id) {
        return -(1 - step * 3e-4);
    } else if (winner == player_id) {
        return 1 - step * 3e-4;
    } else {
        return 0 + step * 3e-4;
    }
}



// minimax 带 alpha-beta 剪枝搜索，返回评估值
// depth: 当前搜索深度
// max_depth: 最大搜索深度
// is_maximizing: 是否为极大化玩家
// alpha: 当前极大化玩家的最好选择
// beta: 当前极小化玩家的最好选择
float AIPlayer::minimax(UltimateTicTacToe& board, int depth, bool is_maximizing, float alpha, float beta) {
    float max_value = -INFINITY;
    float min_value = INFINITY;
    if (board.is_game_over() || depth >= max_depth) {
        return evaluate(board);
    } else if (is_maximizing) {
        for (auto move : board.get_valid_moves()) {
            if (board.make_move(move)) {
                float value = minimax(board, depth + 1, false, alpha, beta);
                max_value = std::max(max_value, value);
                alpha = std::max(alpha, value);
                board.undo_move(move);
                if (beta <= alpha) {
                    return max_value;
                }
            }
        }
    } else {
        for (auto move : board.get_valid_moves()) {
            if (board.make_move(move)) {
                float value = minimax(board, depth + 1, true, alpha, beta);
                min_value = std::min(min_value, value);
                beta = std::min(beta, value);
                board.undo_move(move);
                if (beta <= alpha) {
                    return min_value;
                }
            }
        }
    }
    return is_maximizing ? max_value : min_value;
}
// 获取最佳走子
std::pair<int, int> AIPlayer::get_best_move(UltimateTicTacToe& board) {
    float best_value = -INFINITY;
    std::pair<int, int> best_move = std::make_pair(-1, -1);
    for (auto move : board.get_valid_moves()) {
        if (board.make_move(move)) {
            float move_value = minimax(board, 1, false, -INFINITY, INFINITY);
            board.undo_move(move);
            if (move_value > best_value) {
                best_value = move_value;
                best_move = move;
            }
        }
    }
    return best_move;
}