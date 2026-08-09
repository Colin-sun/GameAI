#include "utt.h"

#include <stdexcept>

using namespace std;

namespace {

int count_meta_wins(
    const array<array<int, META_BOARD_SIZE>, META_BOARD_SIZE>& meta_board,
    int player) {
    int wins = 0;
    for (const auto& row : meta_board) {
        for (const int state : row) {
            wins += state == player;
        }
    }
    return wins;
}

// The project rule is a majority of non-drawn local boards, not a meta line.
// Return 0 for an unfinished game and -1 for a finished draw.
int get_meta_winner(
    const array<array<int, META_BOARD_SIZE>, META_BOARD_SIZE>& meta_board) {
    const int player_one_wins = count_meta_wins(meta_board, 1);
    const int player_two_wins = count_meta_wins(meta_board, 2);
    int resolved_boards = 0;
    int drawn_boards = 0;
    for (const auto& row : meta_board) {
        for (const int state : row) {
            resolved_boards += state != 0;
            drawn_boards += state == 3;
        }
    }

    // Drawn local boards do not count toward the majority threshold, while
    // unresolved boards still can be won by either player.
    const int available_boards = META_BOARD_SIZE * META_BOARD_SIZE - drawn_boards;
    const int win_threshold = available_boards / 2 + 1;
    if (player_one_wins >= win_threshold) {
        return 1;
    }
    if (player_two_wins >= win_threshold) {
        return 2;
    }

    if (resolved_boards == META_BOARD_SIZE * META_BOARD_SIZE) {
        if (player_one_wins > player_two_wins) {
            return 1;
        }
        if (player_two_wins > player_one_wins) {
            return 2;
        }
        return -1;
    }
    return 0;
}

template <typename ActionListType>
int choose_random_action(const ActionListType& actions, mt19937& rand_engine) {
    uniform_int_distribution<int> distribution(0, actions.size() - 1);
    return actions[distribution(rand_engine)];
}

} // namespace

// 检查3x3棋盘状态：0未完成，1玩家X获胜，2玩家O获胜，3平局
int UltimateTicTacToe::get_board_state(const std::array<std::array<int, 3>, 3>& sub_board) const {
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

// 获取大棋盘状态
void UltimateTicTacToe::get_meta_board_state() {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            // Recompute every sub-board so undo can restore a formerly
            // completed board as well as the normal constructor path.
            std::array<std::array<int, 3>, 3> sub_board{};
            for (int x = 0; x < 3; ++x) {
                for (int y = 0; y < 3; ++y) {
                    sub_board[x][y] = board[i * 3 + x][j * 3 + y];
                }
            }
            meta_board[i][j] = get_board_state(sub_board);
        }
    }
}

// 获取row, col 所属的大棋盘坐标
std::pair<int, int> get_meta_board_coord(int row, int col) {
    int sub_row = row / 3;
    int sub_col = col / 3;
    return make_pair(sub_row, sub_col);
}
// 获取大棋盘坐标对应的sub_board
std::array<std::array<int, 3>, 3> UltimateTicTacToe::get_sub_board(int meta_row, int meta_col) const {
    std::array<std::array<int, 3>, 3> sub_board{};
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 3; ++y) {
            sub_board[x][y] = board[meta_row * 3 + x][meta_col * 3 + y];
        }
    }
    return sub_board;
}

// 部分更新大棋盘状态
void UltimateTicTacToe::update_meta_board(int row, int col) {
    std::pair<int, int> coord = get_meta_board_coord(row, col);
    std::array<std::array<int, 3>, 3> sub_board = get_sub_board(coord.first, coord.second);
    int state = get_board_state(sub_board);
    meta_board[coord.first][coord.second] = state;
}



UltimateTicTacToe::UltimateTicTacToe():
    board{}, // 初始化棋盘为空
    meta_board{},
    next_board{-1, -1}, // 初始化为无效位置
    previous_next_board{-1, -1},
    current_player(1), // 初始玩家为 X
    step(0) // 初始步数为 0
{}

// 含参构造函数，用于从特定状态初始化游戏
UltimateTicTacToe::UltimateTicTacToe(NewGameParameters& parameters):
    board {parameters.board},
    meta_board{},
    next_board{parameters.next_board},
    previous_next_board{parameters.next_board},
    current_player{1},
    step{0}
{
    // 自动获取已走的步数
    for (int i = 0; i < 9; ++i) {
        for (int j = 0; j < 9; ++j) {
            if (board[i][j] != 0) {
                step += 1;
            }
        }
    }

    // 根据step获取当前玩家
    if (step % 2 == 0) {
        this->current_player = 1;
    } else {
        this->current_player = 2;
    }

    get_meta_board_state(); // 初始化时获取大棋盘状态
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
    if (get_done_winner().first) {
        return false;
    }

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
const ActionList UltimateTicTacToe::get_valid_actions() const {
    ActionList valid_actions;
    if (get_done_winner().first) {
        return valid_actions;
    }

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
    for (int row = 0; row < BOARD_SIZE; ++row) {
        for (int col = 0; col < BOARD_SIZE; ++col) {
            // 计算所属的小棋盘坐标
            int sub_row = row / 3;
            int sub_col = col / 3;
            // 检查小棋盘是否已完成
            if (meta_board[sub_row][sub_col] == 0 && board[row][col] == 0) {
                valid_actions.emplace_back(get_action_index(row, col));
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

    previous_next_board = next_board;

    // 将 action 转为 row 和 col
    auto [row, col] = get_row_col(action);

    board[row][col] = current_player;

    // 更新大棋盘状态
    update_meta_board(row, col);

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
    // TODO 使用 minimax后端这里可以优化
    get_meta_board_state();

    // 恢复走子前的限定小棋盘，而不是根据被撤销动作重新推导。
    next_board = previous_next_board;

    // 切换玩家
    current_player = 3 - current_player; // 1->2, 2->1
    step -= 1; // 步数减 1
}

// 检查大棋盘获胜者和游戏是否结束：0无，1玩家X获胜，2玩家 O获胜，-1平局
std::pair<bool, int> UltimateTicTacToe::get_done_winner() const {
    const int winner = get_meta_winner(meta_board);
    if (winner != 0) {
        return make_pair(true, winner);
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

int UltimateTicTacToe::select_rollout_action(
    const ActionList& actions,
    std::mt19937& rand_engine,
    int rollout_policy) const {
    if (actions.size() == 0) {
        throw std::runtime_error("Cannot select a rollout action from an empty list");
    }
    if (rollout_policy == 0) {
        return choose_random_action(actions, rand_engine);
    }

    const int player = current_player;
    const int opponent = 3 - player;
    ActionList winning_actions;
    ActionList blocking_actions;
    ActionList local_winning_actions;
    ActionList local_blocking_actions;

    for (int index = 0; index < actions.size(); ++index) {
        const int action = actions[index];
        if (wins_game_with_action(action, player)) {
            winning_actions.emplace_back(action);
        }
        if (wins_game_with_action(action, opponent)) {
            blocking_actions.emplace_back(action);
        }
        if (completes_local_board(action, player)) {
            local_winning_actions.emplace_back(action);
        }
        if (completes_local_board(action, opponent)) {
            local_blocking_actions.emplace_back(action);
        }
    }

    if (winning_actions.size() > 0) {
        return choose_random_action(winning_actions, rand_engine);
    }
    if (blocking_actions.size() > 0) {
        return choose_random_action(blocking_actions, rand_engine);
    }
    if (local_winning_actions.size() > 0) {
        return choose_random_action(local_winning_actions, rand_engine);
    }
    if (local_blocking_actions.size() > 0) {
        return choose_random_action(local_blocking_actions, rand_engine);
    }
    return choose_random_action(actions, rand_engine);
}

float UltimateTicTacToe::evaluate(int player) const {
    if (player != 1 && player != 2) {
        return 0.0f;
    }
    const auto result = get_done_winner();
    if (result.first) {
        if (result.second == -1) {
            return 0.0f;
        }
        return result.second == player ? 1.0f : -1.0f;
    }

    const int opponent = 3 - player;
    // Meta-board lines have no meaning under the majority rule. The primary
    // signal is the number of local boards already won; unresolved local
    // boards contribute only their local tactical potential.
    float score = 0.35f * static_cast<float>(
        count_meta_wins(meta_board, player) - count_meta_wins(meta_board, opponent));
    const int local_lines[8][3][2] = {
        {{0, 0}, {0, 1}, {0, 2}},
        {{1, 0}, {1, 1}, {1, 2}},
        {{2, 0}, {2, 1}, {2, 2}},
        {{0, 0}, {1, 0}, {2, 0}},
        {{0, 1}, {1, 1}, {2, 1}},
        {{0, 2}, {1, 2}, {2, 2}},
        {{0, 0}, {1, 1}, {2, 2}},
        {{0, 2}, {1, 1}, {2, 0}},
    };

    for (int meta_row = 0; meta_row < META_BOARD_SIZE; ++meta_row) {
        for (int meta_col = 0; meta_col < META_BOARD_SIZE; ++meta_col) {
            if (meta_board[meta_row][meta_col] == 0) {
                const auto sub_board = get_sub_board(meta_row, meta_col);
                for (const auto& line : local_lines) {
                    int own = 0;
                    int theirs = 0;
                    for (const auto& cell : line) {
                        const int value = sub_board[cell[0]][cell[1]];
                        own += value == player;
                        theirs += value == opponent;
                    }
                    if (theirs == 0 && own > 0) {
                        score += own == 2 ? 0.055f : 0.012f;
                    } else if (own == 0 && theirs > 0) {
                        score -= theirs == 2 ? 0.055f : 0.012f;
                    }
                }
            }
        }
    }

    return tanh(score);
}

bool UltimateTicTacToe::completes_local_board(int action, int player) const {
    if ((player != 1 && player != 2) || !is_action_valid(action)) {
        return false;
    }
    const auto [row, col] = get_row_col(action);
    auto sub_board = get_sub_board(row / 3, col / 3);
    sub_board[row % 3][col % 3] = player;
    return get_board_state(sub_board) == player;
}

bool UltimateTicTacToe::wins_game_with_action(int action, int player) const {
    if (!completes_local_board(action, player)) {
        return false;
    }
    const auto [row, col] = get_row_col(action);
    auto next_meta_board = meta_board;
    next_meta_board[row / 3][col / 3] = player;
    return get_meta_winner(next_meta_board) == player;
}

// 模型训练用函数，获取当前局面状态相关参数
// 获取棋盘状态
const std::array<std::array<int, BOARD_SIZE>, BOARD_SIZE>& UltimateTicTacToe::get_board() const {
    return board;
}
// 获取子棋盘状态
const std::array<std::array<int, META_BOARD_SIZE>, META_BOARD_SIZE>& UltimateTicTacToe::get_meta_board() const {
    return meta_board;
}
std::pair<int, int> UltimateTicTacToe::get_next_board() const {
    return next_board;
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
