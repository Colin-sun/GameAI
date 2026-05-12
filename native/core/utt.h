// 实现游戏基本逻辑
#pragma once
#include <array>
#include <utility>
#include <random>
#include <cstdint>
#include <iostream>
#include <array>
// #include <torch/torch.h>
#include "base_game.h"

constexpr int BOARD_SIZE = 9; // 棋盘边长
constexpr int META_BOARD_SIZE = 3; // 小棋盘个数：3x3
constexpr int STATE_NUM = 3; // 每个格子的状态数：0空，1X，2O
constexpr int MAX_MOVES = BOARD_SIZE * BOARD_SIZE; // 最大有效动作数

constexpr int IN_CHANNELS = 6; // 输入通道数

// 创建容纳生成测试棋盘参数的结构体
struct NewGameParameters {
    std::array<std::array<int, BOARD_SIZE>, BOARD_SIZE> board;
    std::pair<int, int> next_board; // 下一步可下的小棋盘的位置，-1, -1表示任意位置可下
    // 其他参数可以自动获得
};

// 存储有效动作数的数组
struct ActionList {
    int  data[MAX_MOVES];
    int  count = 0;
    int  size() const noexcept { return count; }
    int& operator[](int i)       noexcept { return data[i]; }
    int  operator[](int i) const noexcept { return data[i]; }
    void emplace_back(int action_num) noexcept {
        data[count] = action_num;
        count++;
    }

};

class UltimateTicTacToe : public GameBase<UltimateTicTacToe, ActionList> {
private:
    // 9x9棋盘，0表示空，1表示先手，2表示后手
    std::array<std::array<int, BOARD_SIZE>, BOARD_SIZE> board;
    // 3x3小棋盘状态，0表示未完成，1表示先手获胜，2表示后手，3表示平局
    std::array<std::array<int, META_BOARD_SIZE>, META_BOARD_SIZE> meta_board;
    // 当前应该下棋的小棋盘位置，(-1, -1)表示可以任意位置下棋
    std::pair<int, int> next_board;
    int current_player; // 玩家名，1为先手（红色圆圈），2为后手（蓝叉）
    // 步数（评估函数使用）
    int step;

    // 检查3x3棋盘状态：0未完成，1玩家X获胜，2玩家O获胜，3平局
    int get_board_state(const std::array<std::array<int, 3>, 3>& board_3x3);
    // 更新大棋盘状态
    void update_meta_board(int row, int col);
    // 获取大棋盘状态
    void get_meta_board_state();

    // 获取大棋盘坐标对应的sub_board
    std::array<std::array<int, 3>, 3> get_sub_board(int meta_row, int meta_col) const;

public:
    // 默认构造空棋盘
    UltimateTicTacToe();
    // 含参构造函数
    UltimateTicTacToe(NewGameParameters& parameters);

    // 棋盘行列坐标 和 action_index 互转
    int get_action_index(int row, int col) const;
    std::pair<int, int> get_row_col(int action_index) const;

    // 实现所有纯虚函数
    const ActionList get_valid_actions() const override;
    bool is_action_valid(int action) const override;
    bool make_move(int action) override;
    std::pair<bool, int> get_done_winner() const override;
    int get_current_player() const override;
    std::shared_ptr<UltimateTicTacToe> clone() const override;
    // std::tuple<std::vector<int>, std::vector<float>, float> policy_value_fn() const override;

    // alpha-bata使用
    void undo_move(std::pair<int, int> move); // 撤销走子，只能撤销最近走的一步
    // 训练相关函数
    const std::array<std::array<int, BOARD_SIZE>, BOARD_SIZE>& get_board() const; // 获取棋盘状态
    const std::array<std::array<int, META_BOARD_SIZE>, META_BOARD_SIZE>& get_meta_board() const; // 获取子棋盘状态
    std::pair<int, int> get_next_board() const; // 获取下一步限定的小棋盘
    int get_step() const; // 获取当前步数

    // 展示函数
    void print_board() const;
};

// // 将棋盘状态转为 6通道张量
// // 当前玩家指的是即将落子的一方
// // 这个函数和游戏逻辑强相关，所以放在这里
// torch::Tensor board_to_tensor(const UltimateTicTacToe& current_game);
