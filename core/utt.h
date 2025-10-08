// 实现游戏基本逻辑
#pragma once

#include <vector>
#include <utility>
#include <random>
#include <cstdint>
#include <iostream>
#include <array>
#include <torch/torch.h>

const int BOARD_SIZE = 9; // 棋盘边长
const int META_BOARD_SIZE = 3; // 小棋盘个数：3x3
const int STATE_NUM = 3; // 每个格子的状态数：0空，1X，2O

const int IN_CHANNELS = 6; // 输入通道数

// 创建容纳生成测试棋盘参数的结构体
struct NewGameParameters {
    std::vector<std::vector<int>> board;
    std::pair<int, int> next_board; // 下一步可下的小棋盘的位置，-1, -1表示任意位置可下
    int current_player; // 1为 X，2为 O
};

class UltimateTicTacToe {
private:
    // 9x9棋盘，0表示空，1表示玩家X，2表示玩家O
    std::vector<std::vector<int>> board;
    // 3x3小棋盘状态，0表示未完成，1表示X获胜，2表示O获胜，3表示平局
    std::vector<std::vector<int>> meta_board;
    // 当前应该下棋的小棋盘位置，(-1, -1)表示可以任意位置下棋
    std::pair<int, int> next_board;
    int current_player; // 1为 X，2为 O
    // 检查3x3棋盘状态：0未完成，1玩家X获胜，2玩家O获胜，3平局
    int get_board_state(const std::vector<std::vector<int>>& board_3x3);
    // 更新大棋盘状态
    void update_meta_board();
    // 步数（评估函数使用）
    int step;

public:
    // 默认构造空棋盘
    UltimateTicTacToe();
    // 含参构造函数
    UltimateTicTacToe(NewGameParameters& parameters);
    // 检查在九宫格棋盘上的指定位置是否为有效移动
    bool is_valid_move(int row, int col) const;
    // 获取所有有效移动
    const std::vector<std::pair<int, int>> get_valid_moves() const;
    // 执行移动，同时更新步数，返回移动是否成功
    bool make_move(std::pair<int, int> move);
    // 检查大棋盘获胜者：0无，1玩家X获胜，2玩家O获胜，3平局
    int get_winner() const;
    // 检查游戏是否结束
    bool is_game_over() const;
    void undo_move(std::pair<int, int> move); // 撤销走子，只能撤销最近走的一步

    // 训练相关函数
    const std::vector<std::vector<int>>& get_board() const; // 获取棋盘状态
    const std::vector<std::vector<int>>& get_meta_board() const; // 获取子棋盘状态
    int get_step() const; // 获取当前步数
    int get_current_player() const; // 获取当前玩家

    // 展示函数
    void print_board() const;
};

// 将棋盘状态转为 6通道张量
// 当前玩家指的是即将落子的一方
// 这个函数和游戏逻辑强相关，所以放在这里
torch::Tensor board_to_tensor(const UltimateTicTacToe& current_game);
