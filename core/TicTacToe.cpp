#include "TicTacToe.h"

using namespace std;
UltimateTicTacToe::UltimateTicTacToe() {
    board = vector<vector<int>>(3, vector<int>(3, 0));
    current_player = 1; // 初始玩家为 X
    step = 0; // 初始步数为 0
}

// 含参构造函数，用于从特定状态初始化游戏
UltimateTicTacToe::UltimateTicTacToe(NewGameParameters& parameters) {
    this->board = parameters.board;
    this->current_player = parameters.current_player;
    step = 0;
    // 自动获取已走的步数
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (board[i][j] != 0) {
                step += 1;
            }
        }
    }
    this->step = step;
}

// 检查在九宫格棋盘上的指定位置是否为有效移动
bool UltimateTicTacToe::is_valid_move(int row, int col) const {
    // 检查坐标是否合法
    if (row < 0 || row >= 3 || col < 0 || col >= 3) {
        return false;
    }

    // 检查位置是否为空
    return board[row][col] == 0;
}

// 获取当前局面下所有有效移动
const vector<pair<int, int>> UltimateTicTacToe::get_valid_moves() const {
    vector<pair<int, int>> moves;

    // 已完成则可以在任何未完成的小棋盘中下棋
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (is_valid_move(i, j)) {
                moves.emplace_back(i, j);
            }
        }
    }

    return moves;
}

// 执行移动
bool UltimateTicTacToe::make_move(std::pair<int, int> move) {
    int row = move.first;
    int col = move.second;
    if (!is_valid_move(row, col)) {
        return false;
    }

    board[row][col] = current_player;

    // 切换玩家
    current_player = 3 - current_player; // 1->2, 2->1
    step += 1; // 步数加 1
    return true;
}

void UltimateTicTacToe::undo_move(std::pair<int, int> move) {
    int row = move.first;
    int col = move.second;
    board[row][col] = 0;

    // 切换玩家
    current_player = 3 - current_player; // 1->2, 2->1
    step -= 1; // 步数减 1
}

// 检查获胜者：0无，1玩家X获胜，2玩家 O获胜，3平局
int UltimateTicTacToe::get_winner() const {
    // 检查对角线 1
    if (board[0][0] != 0 && board[0][0] == board[1][1] && board[1][1] == board[2][2]) {
        return board[0][0];
    }
    // 检查对角线 2
    if (board[0][2] != 0 && board[0][2] == board[1][1] && board[1][1] == board[2][0]) {
        return board[0][2];
    }

    // 检查行和列
    for (int i = 0; i < 3; ++i) {
        // 检查行
        if (board[i][0] != 0 && board[i][0] == board[i][1] && board[i][1] == board[i][2]) {
            return board[i][0];
        }
        // 检查列
        if (board[0][i] != 0 && board[0][i] == board[1][i] && board[1][i] == board[2][i]) {
            return board[0][i];
        }
    }
    // 检查平局
    return get_valid_moves().empty() ? 3 : 0;
}

// 检查游戏是否结束
bool UltimateTicTacToe::is_game_over() const {
    return get_winner() != 0 || get_valid_moves().empty();
}

// 模型训练用函数，获取当前局面状态相关参数
// 获取棋盘状态
const std::vector<std::vector<int>>& UltimateTicTacToe::get_board() const {
    return board;
}

// 获取当前步数
int UltimateTicTacToe::get_step() const {
    return step;
}
// 获取当前玩家
int UltimateTicTacToe::get_current_player() const {
    return current_player;
}

// 展示函数
void UltimateTicTacToe::print_board() const {
    std::cout << "---------------------\n";
    for (int i = 0; i < 3; ++i) {
        if (i % 3 == 0 && i != 0) {
            std::cout << "---------------------\n";
        }
        for (int j = 0; j < 3; ++j) {
            if (j % 3 == 0 && j != 0) {
                std::cout << "| ";
            }
            char mark = (board[i][j] == 0) ? '.' : (board[i][j] == 1 ? 'X' : 'O');
            std::cout << mark << ' ';
        }
        std::cout << '\n';
    }
    std::cout << '\n';
}

// 棋盘状态转张量
torch::Tensor board_to_tensor(const UltimateTicTacToe& current_game) {
    auto options = torch::TensorOptions().dtype(torch::kFloat32);

    // 获取棋盘状态
    const auto& board = current_game.get_board();
    auto current_player = current_game.get_current_player();
    if (board.empty()) {
        throw std::runtime_error("Invalid board data");
    }

    // 初始化张量
    auto board_player = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);
    auto board_opponent = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);
    auto board_empty = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);

    // 填充每个通道的值
    for (int i = 0; i < BOARD_SIZE; ++i) {
        for (int j = 0; j < BOARD_SIZE; ++j) {
            board_player[i][j] = (board[i][j] == current_player) ? 1.0f : 0.0f;
            board_opponent[i][j] = (board[i][j] == 3 - current_player) ? 1.0f : 0.0f;
            board_empty[i][j] = (board[i][j] == 0) ? 1.0f : 0.0f;
        }
    }

    // 堆叠成6通道的张量
    return torch::stack({
        board_player, board_opponent, board_empty
        }, 0);
}