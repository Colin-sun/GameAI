#include "utt.h"

using namespace std;

// 检查3x3棋盘状态：0未完成，1玩家X获胜，2玩家O获胜，3平局
int UltimateTicTacToe::get_board_state(const vector<vector<int>>& sub_board) {
    // 检查行
    for (int i = 0; i < META_BOARD_SIZE; ++i) {
        if (sub_board[i][0] == sub_board[i][1] && sub_board[i][1] == sub_board[i][2] && sub_board[i][0] != 0) {
            return sub_board[i][0];
        }
    }

    // 检查列
    for (int j = 0; j < META_BOARD_SIZE; ++j) {
        if (sub_board[0][j] == sub_board[1][j] && sub_board[1][j] == sub_board[2][j] && sub_board[0][j] != 0) {
            return sub_board[0][j];
        }
    }

    // 检查对角线
    if (sub_board[0][0] == sub_board[1][1] && sub_board[1][1] == sub_board[2][2] && sub_board[0][0] != 0) {
        return sub_board[0][0];
    }
    if (sub_board[0][2] == sub_board[1][1] && sub_board[1][1] == sub_board[2][0] && sub_board[0][2] != 0) {
        return sub_board[0][2];
    }

    // 检查是否可下
    for (int i = 0; i < META_BOARD_SIZE; ++i) {
        for (int j = 0; j < META_BOARD_SIZE; ++j) {
            if (sub_board[i][j] == 0) {
                return 0; // 还有空位
            }
        }
    }

    return 3; // 平局
}

// 更新大棋盘状态
void UltimateTicTacToe::update_meta_board() {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (meta_board[i][j] == 0) { // 只检查未完成的小棋盘
                // 提取3x3子棋盘
                vector<vector<int>> sub_board(3, vector<int>(3));
                for (int x = 0; x < 3; ++x) {
                    for (int y = 0; y < 3; ++y) {
                        sub_board[x][y] = board[i * 3 + x][j * 3 + y];
                    }
                }
                int state = get_board_state(sub_board);
                if (state != 0) {
                    meta_board[i][j] = state;
                }
            }
        }
    }
}

UltimateTicTacToe::UltimateTicTacToe() {
    this->board = vector<vector<int>>(BOARD_SIZE, vector<int>(BOARD_SIZE, 0));
    this->meta_board = vector<vector<int>>(META_BOARD_SIZE, vector<int>(META_BOARD_SIZE, 0));
    this->next_board = make_pair(-1, -1); // 初始化为无效位置
    this->current_player = 1; // 初始玩家为 X
    this->step = 0; // 初始步数为 0
}

// 含参构造函数，用于从特定状态初始化游戏
UltimateTicTacToe::UltimateTicTacToe(NewGameParameters& parameters) {
    this->board = parameters.board;
    this->meta_board = vector<vector<int>>(3, vector<int>(3, 0));
    this->next_board = parameters.next_board;

    int step = 0;
    // 自动获取已走的步数
    for (int i = 0; i < 9; ++i) {
        for (int j = 0; j < 9; ++j) {
            if (board[i][j] != 0) {
                step += 1;
            }
        }
    }
    this->step = step;

    // 根据step获取当前玩家
    if (step % 2 == 0) {
        this->current_player = 1;
    } else {
        this->current_player = 2;
    }

    update_meta_board(); // 初始化时更新大棋盘状态
}

int UltimateTicTacToe::get_action_index(int row, int col) const {
    return row * BOARD_SIZE + col;
}

std::pair<int, int> UltimateTicTacToe::get_row_col(int action_index) const {
    int row = action_index / BOARD_SIZE;
    int col = action_index % BOARD_SIZE;
    return make_pair(row, col);
}


// 检查在九宫格棋盘上的指定位置是否为有效移动
bool UltimateTicTacToe::is_action_valid(int action) const {
    // 将 action 转为 row 和 col
    auto [row, col] = get_row_col(action);

    // 检查坐标是否合法
    if (row < 0 || row >= 9 || col < 0 || col >= 9) {
        return false;
    }

    // 计算所属的小棋盘坐标
    int sub_row = row / 3;
    int sub_col = col / 3;

    // 检查小棋盘是否已完成
    if (meta_board[sub_row][sub_col] != 0) {
        return false;
    }

    // 检查是否在指定的小棋盘中（如果有限制）
    if (next_board.first != -1 && next_board.second != -1) {
        if (sub_row != next_board.first || sub_col != next_board.second) {
            return false;
        }
    }

    // 检查位置是否为空
    return board[row][col] == 0;
}

// 获取当前局面下所有有效移动
const std::vector<int> UltimateTicTacToe::get_valid_actions() const {
    vector<int> valid_actions;
    // 如果指定了必须下的小棋盘
    if (next_board.first != -1 && next_board.second != -1) {
        int sub_row = next_board.first;
        int sub_col = next_board.second;
        // 检查该小棋盘是否已完成
        if (meta_board[sub_row][sub_col] == 0) {
            for (int i = 0; i < META_BOARD_SIZE; ++i) {
                for (int j = 0; j < META_BOARD_SIZE; ++j) {
                    int row = sub_row * META_BOARD_SIZE + i;
                    int col = sub_col * META_BOARD_SIZE + j;
                    if (board[row][col] == 0) {
                        valid_actions.emplace_back(get_action_index(row, col));
                    }
                }
            }
            return valid_actions;
        }
    }

    // 已完成则可以在任何未完成的小棋盘中下棋
    for (int i = 0; i < BOARD_SIZE; ++i) {
        for (int j = 0; j < BOARD_SIZE; ++j) {
            int action = get_action_index(i, j);
            if (is_action_valid(action)) {
                valid_actions.emplace_back(action);
            }
        }
    }

    return valid_actions;
}

// 执行移动
bool UltimateTicTacToe::make_move(int action) {
    if (!is_action_valid(action)) {
        return false;
    }

    // 将 action 转为 row 和 col
    auto [row, col] = get_row_col(action);

    board[row][col] = current_player;

    // 更新大棋盘状态
    update_meta_board();

    // 设置下一个玩家必须下的小棋盘
    int local_row = row % 3;
    int local_col = col % 3;
    if (meta_board[local_row][local_col] == 0) {
        next_board = make_pair(local_row, local_col);
    } else {
        next_board = make_pair(-1, -1); // 重置为无效位置
    }

    // 切换玩家
    current_player = 3 - current_player; // 1->2, 2->1
    step += 1; // 步数加 1
    return true;
}

void UltimateTicTacToe::undo_move(std::pair<int, int> move) {
    int row = move.first;
    int col = move.second;
    board[row][col] = 0;
    // 更新大棋盘状态
    update_meta_board();

    // 设置下一个玩家必须下的小棋盘
    int local_row = row % 3;
    int local_col = col % 3;
    if (meta_board[local_row][local_col] == 0) {
        next_board = std::make_pair(local_row, local_col);
    } else {
        next_board = std::make_pair(-1, -1); // 重置为无效位置
    }

    // 切换玩家
    current_player = 3 - current_player; // 1->2, 2->1
    step -= 1; // 步数减 1
}

// 检查大棋盘获胜者和游戏是否结束：0无，1玩家X获胜，2玩家 O获胜，-1平局
std::pair<bool, int> UltimateTicTacToe::get_done_winner() const {
    int player_x_wins = 0;
    int player_o_wins = 0;
    int total_boards_available = 9; // 去除平局的小棋盘数量

    // 统计每个玩家赢得的小棋盘数量
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (meta_board[i][j] == 1) {
                player_x_wins += 1;
            } else if (meta_board[i][j] == 2) {
                player_o_wins += 1;
            } else if (meta_board[i][j] == 3) {
                total_boards_available -= 1;
            }
        }
    }
    int win_threshold = total_boards_available / 2 + 1; // 获胜所需的小棋盘数量

    // 检查是否有玩家达到获胜条件
    // 获胜条件：去除平局的小棋盘，获得超过半数的小棋盘
    if (player_x_wins >= win_threshold) {
        return make_pair(true, 1);
    } else if (player_o_wins >= win_threshold) {
        return make_pair(true, 2);
    }

    // 如果棋盘已满且没有玩家达到获胜条件，则平局
    if (player_x_wins + player_o_wins == total_boards_available) {
        return make_pair(true, -1);
    }

    // 游戏尚未结束
    return make_pair(false, 0);
}

// 获取当前玩家
int UltimateTicTacToe::get_current_player() const {
    return current_player;
}

std::shared_ptr<UltimateTicTacToe> UltimateTicTacToe::clone() const {
    return std::make_shared<UltimateTicTacToe>(*this);
}

// 模型训练用函数，获取当前局面状态相关参数
// 获取棋盘状态
const std::vector<std::vector<int>>& UltimateTicTacToe::get_board() const {
    return board;
}
// 获取子棋盘状态
const std::vector<std::vector<int>>& UltimateTicTacToe::get_meta_board() const {
    return meta_board;
}
// 获取当前步数
int UltimateTicTacToe::get_step() const {
    return step;
}


// 展示函数
void UltimateTicTacToe::print_board() const {
    std::cout << "---------------------\n";
    for (int i = 0; i < 9; ++i) {
        if (i % 3 == 0 && i != 0) {
            std::cout << "---------------------\n";
        }
        for (int j = 0; j < 9; ++j) {
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

// // 棋盘状态转张量
// // TODO 加入next_board的表示
// torch::Tensor board_to_tensor(const UltimateTicTacToe& current_game) {
//     auto options = torch::TensorOptions().dtype(torch::kFloat32);

//     // 获取棋盘状态
//     const auto& board = current_game.get_board();
//     const auto& meta_board = current_game.get_meta_board();
//     auto current_player = current_game.get_current_player();
//     if (board.empty() || meta_board.empty()) {
//         throw std::runtime_error("Invalid board data");
//     }

//     // 初始化张量6个通道
//     auto board_player = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);
//     auto board_opponent = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);
//     auto board_empty = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);

//     // 这里额外放大了向量的维数
//     auto meta_board_player = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);
//     auto meta_board_opponent = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);
//     auto meta_board_empty = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);

//     // 填充每个通道的值
//     for (int i = 0; i < BOARD_SIZE; ++i) {
//         for (int j = 0; j < BOARD_SIZE; ++j) {
//             board_player[i][j] = (board[i][j] == current_player) ? 1.0f : 0.0f;
//             board_opponent[i][j] = (board[i][j] == 3 - current_player) ? 1.0f : 0.0f;
//             board_empty[i][j] = (board[i][j] == 0) ? 1.0f : 0.0f;
//         }
//     }

//     for (int i = 0; i < META_BOARD_SIZE; ++i) {
//         for (int j = 0; j < META_BOARD_SIZE; ++j) {
//             meta_board_player[i][j] = (meta_board[i][j] == current_player) ? 1.0f : 0.0f;
//             meta_board_opponent[i][j] = (meta_board[i][j] == 3 - current_player) ? 1.0f : 0.0f;
//             meta_board_empty[i][j] = (meta_board[i][j] == 0) ? 1.0f : 0.0f;
//         }
//     }

//     // 堆叠成6通道的张量
//     return torch::stack({
//         board_player, board_opponent, board_empty,
//         meta_board_player, meta_board_opponent, meta_board_empty
//         }, 0);
// }
