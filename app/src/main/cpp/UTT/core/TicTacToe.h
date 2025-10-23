// 井字棋测试代码
#pragma once
#include <vector>
#include <utility>
#include <random>
#include <cstdint>
#include <iostream>
#include <array>
#include <torch/torch.h>

const int BOARD_SIZE = 3; // 棋盘边长
const int STATE_NUM = 3; // 每个格子的状态数：0空，1X，2O

const int IN_CHANNELS = 3; // 输入通道数

// 创建容纳生成测试棋盘参数的结构体
struct NewGameParameters {
    std::vector<std::vector<int>> board;
    int current_player; // 1为 X，2为 O
};

class UltimateTicTacToe {
private:
    // 棋盘，0表示空，1表示玩家X，2表示玩家O
    std::vector<std::vector<int>> board;
    int current_player; // 1为 X，2为 O
    // 步数（评估函数使用）
    int step;

public:
    // 默认构造空棋盘
    UltimateTicTacToe();
    // 含参构造函数 
    UltimateTicTacToe(NewGameParameters& parameters);
    // 检查在棋盘上的指定位置是否为有效移动
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
    int get_step() const; // 获取当前步数
    int get_current_player() const; // 获取当前玩家

    // 展示函数
    void print_board() const;
};

// 将棋盘状态转为 6通道张量
// 当前玩家指的是即将落子的一方
// 这个函数和游戏逻辑强相关，所以放在这里
torch::Tensor board_to_tensor(const UltimateTicTacToe& current_game);

