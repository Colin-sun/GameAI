#include <vector>
#include <array>
#include "game.h"

using namespace std;

// 检查3x3棋盘状态：0未完成，1玩家X获胜，2玩家O获胜，3平局
int UltimateTicTacToe::get_board_state(const vector<vector<int>>& board_3x3) {
    // 检查行
    for (int i = 0; i < 3; ++i) {
        if (board_3x3[i][0] == board_3x3[i][1] && board_3x3[i][1] == board_3x3[i][2] && board_3x3[i][0] != 0) {
            return board_3x3[i][0];
        }
    }

    // 检查列
    for (int j = 0; j < 3; ++j) {
        if (board_3x3[0][j] == board_3x3[1][j] && board_3x3[1][j] == board_3x3[2][j] && board_3x3[0][j] != 0) {
            return board_3x3[0][j];
        }
    }

    // 检查对角线
    if (board_3x3[0][0] == board_3x3[1][1] && board_3x3[1][1] == board_3x3[2][2] && board_3x3[0][0] != 0) {
        return board_3x3[0][0];
    }
    if (board_3x3[0][2] == board_3x3[1][1] && board_3x3[1][1] == board_3x3[2][0] && board_3x3[0][2] != 0) {
        return board_3x3[0][2];
    }

    // 检查是否平局
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (board_3x3[i][j] == 0) {
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
    board = vector<vector<int>>(9, vector<int>(9, 0));
    meta_board = vector<vector<int>>(3, vector<int>(3, 0));
    next_board = make_pair(-1, -1); // 初始化为无效位置
    current_player = 1; // 初始玩家为 X
    step = 0; // 初始步数为 0
}

// 含参构造函数，用于从特定状态初始化游戏
UltimateTicTacToe::UltimateTicTacToe(const vector<vector<int>>& board,
    pair<int, int> next_board,
    int current_player, // 1为 X，2为O
    int step) {
    this->board = board;
    this->meta_board = vector<vector<int>>(3, vector<int>(3, 0));
    this->next_board = next_board;
    this->current_player = current_player;
    this->step = step;
    update_meta_board(); // 初始化时更新大棋盘状态
}

// 检查在九宫格棋盘上的指定位置是否为有效移动
bool UltimateTicTacToe::is_valid_move(int row, int col) {
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
vector<pair<int, int>> UltimateTicTacToe::get_valid_moves() {
    vector<pair<int, int>> moves;

    // 如果指定了必须下的小棋盘
    if (next_board.first != -1 && next_board.second != -1) {
        int sub_row = next_board.first;
        int sub_col = next_board.second;
        // 检查该小棋盘是否已完成
        if (meta_board[sub_row][sub_col] == 0) {
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    int row = sub_row * 3 + i;
                    int col = sub_col * 3 + j;
                    if (board[row][col] == 0) {
                        moves.emplace_back(row, col);
                    }
                }
            }
            return moves;
        }
    }

    // 已完成则可以在任何未完成的小棋盘中下棋
    for (int i = 0; i < 9; ++i) {
        for (int j = 0; j < 9; ++j) {
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

// 检查大棋盘获胜者：0无，1玩家X获胜，2玩家 O获胜，3平局
int UltimateTicTacToe::get_winner() {
    int player_x_wins = 0;
    int player_o_wins = 0;
    int draw = 0; //
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
        return 1;
    } else if (player_o_wins >= win_threshold) {
        return 2;
    }

    // 如果棋盘已满且没有玩家达到获胜条件，则平局
    if (player_x_wins + player_o_wins == total_boards_available) {
        return 3;
    }

    // 游戏尚未结束
    return 0;
}

// 检查游戏是否结束
bool UltimateTicTacToe::is_game_over() {
    return get_winner() != 0 || get_valid_moves().empty();
}

// 模型训练用函数，获取当前局面状态相关参数
// 获取棋盘状态
std::vector<std::vector<int>> UltimateTicTacToe::get_board() {
    return board;
}
// 获取子棋盘状态
std::vector<std::vector<int>> UltimateTicTacToe::get_meta_board() {
    return meta_board;
}
// 获取当前步数
int UltimateTicTacToe::get_step() {
    return step;
}
// 获取当前玩家
int UltimateTicTacToe::get_current_player() {
    return current_player;
}

// 展示函数
void UltimateTicTacToe::print_board() {
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