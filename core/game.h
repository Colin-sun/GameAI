// 实现游戏基本逻辑和Zonbrist哈希函数
#pragma once

#include <vector>
#include <utility>
#include <random>
#include <cstdint>
#include <iostream>

const int BOARD_SIZE = 9; // 棋盘边长
const int META_BOARD_SIZE = 3; // 小棋盘个数：3x3
const int STATE_NUM = 3; // 每个格子的状态数：0空，1X，2O

// 前置声明AIPlayer类，以便于实现 undo_move
class AIPlayer;

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
    friend class AIPlayer;

public:
    // 默认构造空棋盘
    UltimateTicTacToe();
    // 含参构造函数
    UltimateTicTacToe(const std::vector<std::vector<int>>& board,
        std::pair<int, int> next_board, // 下一步可下的小棋盘的位置，-1, -1表示任意位置可下
        int current_player, // 1为 X，2为O
        int step);
    // 检查在九宫格棋盘上的指定位置是否为有效移动
    bool is_valid_move(int row, int col);
    // 获取所有有效移动
    std::vector<std::pair<int, int>> get_valid_moves();
    // 执行移动，同时更新步数，返回移动是否成功
    bool make_move(std::pair<int, int> move);
    // 检查大棋盘获胜者：0无，1玩家X获胜，2玩家O获胜，3平局
    int get_winner();
    // 检查游戏是否结束
    bool is_game_over();

    // 训练相关函数
    std::vector<std::vector<int>> get_board(); // 获取棋盘状态
    std::vector<std::vector<int>> get_meta_board(); // 获取子棋盘状态
    int get_step(); // 获取当前步数
    int get_current_player(); // 获取当前玩家

    // 展示函数
    void print_board();
};
