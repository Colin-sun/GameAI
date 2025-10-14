// 客户端主程序：模型推理
#include "json5cpp.h"
#include "utt.h"
#include "MCTS/TreeNode.h"

#ifdef MCTS_PURE
#include "MCTS/mcts_pure.h"
#endif

#include "MCTS/mcts_pure.h" // develop use

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <random>

// 解析配置文件
class Config {
public:
    Config(std::string config_path) {
        // 读文件
        std::ifstream config_file;
        config_file.open(config_path);
        if (!config_file) {
            throw std::runtime_error("无法打开配置文件: " + config_path);
        }
        // 解析 JSON5
        Json::Value parsed_json;
        std::string err;
        if (!Json5::parse(config_file, parsed_json, &err)) {
            config_file.close();
            throw std::runtime_error("配置文件解析错误: " + err);
        }
        config_file.close();
        config_json = parsed_json; // 将解析结果赋值给成员变量
    };

    // 根据参数获取 MCTSPure结构体
    MCTSPure<UltimateTicTacToe> getMCTSPure() {
        // 创建随机数生成器
        std::random_device rd;
        std::mt19937 rand_engine(rd());
        return MCTSPure<UltimateTicTacToe>(
            config_json["mcts"]["n_playout"].asInt(),
            config_json["mcts"]["c_puct"].asFloat(),
            rand_engine
        );
    }

    // 获取 AI 执子方
    int getAIPlayer() {
        return config_json["ai_player"].asInt();
    }

private:
    Json::Value config_json;
};

// 返回值：成功：action_index；失败：返回 -1
int readMove(UltimateTicTacToe& board) {
    std::cout << "请输入您的走子: (0索引)";
    std::string s;
    if (!std::getline(std::cin, s)) {
        std::cout << "输入错误，请重新输入\n";
        return -1;
    }

    int x, y;
    // 必须严格匹配左右括号、逗号、两个整数
    if (std::sscanf(s.c_str(), " (%d ,%d )", &x, &y) != 2) {
        std::cout << "输入错误，请重新输入\n";
        return -1;
    }
    int action_index = board.get_action_index(x, y);
    if (!board.is_action_valid(action_index)) {
        std::cout << "输入位置不合法，请重新输入\n";
        return -1;
    }
    return action_index;
}


// 游戏主函数
int main(int argc, char* argv[]) {
    // 读取配置文件
    std::string configPath;
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "-config" && i + 1 < argc) {
            configPath = argv[i + 1];
            std::cout << "读取配置文件: " << configPath << '\n';
            break;
        }
    }
    if (configPath.empty()) {
        std::cerr << "请使用 -config <路径> 指定配置文件\n";
        return EXIT_FAILURE;
    }
    Config config(configPath);

    // 创建初始棋盘
    UltimateTicTacToe board;
    // 设置AI执子方
    int ai_player = config.getAIPlayer();
    MCTSPure<UltimateTicTacToe> aiplayer = config.getMCTSPure();

    std::cout << "初始化完成\n";

    std::cout << "AI执子方: " << ai_player << std::endl;
    std::cout << "对弈开始\n";
    // 输出空棋盘
    board.print_board();
    // 对弈循环
    while (!board.get_done_winner().first) {
        if (ai_player == board.get_current_player()) {
            // AI 走子
            std::cout << "AI 正在思考...\n";
            UltimateTicTacToe current_game = board;
            int action = aiplayer.get_move(current_game);
            board.make_move(action);

            // action 转 move
            std::pair<int, int> move = board.get_row_col(action);
            std::cout << "AI 走子: (" << move.first << ',' << move.second << ")\n";
        } else {
            int action = readMove(board);
            while (action == -1) {
                action = readMove(board);  // 输入错误，重新输入 
            }
            board.make_move(action);
        }
        board.print_board();
    }
    std::cout << "对弈结束，赢家为: " << board.get_done_winner().second << std::endl;
    return 0;
}