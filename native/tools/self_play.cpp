// 自我对弈程序，调参使用
#include "utt.h"
#include "MCTS/TreeNode.h"

#ifdef MCTS_PURE
#include "MCTS/mcts_pure.h"
#endif

#include "MCTS/mcts_pure.h" // develop use

#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <omp.h>

int main(int argc, char* argv[]) {
    // 读取 MCTS 参数
    if (argc != 5) {
        std::cerr << "请使用 <n_playout1> <c_puct1> <n_playout2> <c_puct2> 启动程序\n";
        return EXIT_FAILURE;
    }
    int n_playout1 = std::stoi(argv[1]);
    float c_puct1 = std::stof(argv[2]);
    int n_playout2 = std::stoi(argv[3]);
    float c_puct2 = std::stof(argv[4]);

    // 并行玩 8 + 8 局
    // aiplayer1 先手
    float win_count_param_1 = 0;
    float win_count_param_2 = 0;
#pragma omp parallel for schedule(static) reduction(+:win_count_param_1, win_count_param_2)
    for (int i = 0; i < 8; ++i) {
        // 创建两个MCTS对象
        thread_local std::mt19937 rand_engine1(std::random_device{}());
        thread_local std::mt19937 rand_engine2(std::random_device{}());
        thread_local auto aiplayer1 = MCTSPure<UltimateTicTacToe, ActionList>(n_playout1, c_puct1, rand_engine1);
        thread_local auto aiplayer2 = MCTSPure<UltimateTicTacToe, ActionList>(n_playout2, c_puct2, rand_engine2);

        std::shared_ptr<UltimateTicTacToe> game = std::make_shared<UltimateTicTacToe>();
        int current_player = game->get_current_player();

        std::pair<bool, int> done_winner;
        while (!(done_winner = game->get_done_winner()).first) {
            if (current_player == 1) {
                game->make_move(aiplayer1.get_move(*game));
            } else {
                game->make_move(aiplayer2.get_move(*game));
            }
            current_player = game->get_current_player(); // 更新当前玩家
        }


        int winner = done_winner.second;
        if (winner == 1) {
            win_count_param_1 += 1;
        } else if (winner == 2) {
            win_count_param_2 += 1;
        } else // 平局
        {
            win_count_param_1 += 0.5;
            win_count_param_2 += 0.5;
        }
    }

    // aiplayer2 先手
#pragma omp parallel for schedule(static) reduction(+:win_count_param_1, win_count_param_2)
    for (int i = 0; i < 8; ++i) {
        thread_local std::mt19937 rand_engine1(std::random_device{}());
        thread_local std::mt19937 rand_engine2(std::random_device{}());
        thread_local auto aiplayer1 = MCTSPure<UltimateTicTacToe, ActionList>(n_playout2, c_puct2, rand_engine2);
        thread_local auto aiplayer2 = MCTSPure<UltimateTicTacToe, ActionList>(n_playout1, c_puct1, rand_engine1);

        std::shared_ptr<UltimateTicTacToe> game = std::make_shared<UltimateTicTacToe>();
        int current_player = game->get_current_player();

        std::pair<bool, int> done_winner;
        while (!(done_winner = game->get_done_winner()).first) {
            if (current_player == 1) {
                game->make_move(aiplayer1.get_move(*game));
            } else {
                game->make_move(aiplayer2.get_move(*game));
            }
            current_player = game->get_current_player(); // 更新当前玩家
        }

        int winner = done_winner.second;
        if (winner == 1) {
            win_count_param_2 += 1;
        } else if (winner == 2) {
            win_count_param_1 += 1;
        } else // 平局
        {
            win_count_param_1 += 0.5;
            win_count_param_2 += 0.5;
        }
    }

    printf("win_count_param_1=%f\n", win_count_param_1);
    printf("win_count_param_2=%f\n", win_count_param_2);
}
