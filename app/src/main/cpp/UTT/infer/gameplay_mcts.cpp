// 客户端主程序：模型推理
#include <torch/torch.h>
#include "json5cpp.h"
#include "game.h"
#include "mcts.h"
#include "network.hpp"

#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <filesystem>

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

    // 获取模型
    ValueCNN getModel() {
        auto model_json = config_json["model"];
        int in_channels = IN_CHANNELS;
        int input_hidden_channels = model_json.get("input_hidden_channels", 64).asInt();
        int num_residual_blocks = model_json.get("num_residual_blocks", 3).asInt();
        int policy_hidden_channels = model_json.get("policy_hidden_channels", 32).asInt();
        int value_dim = model_json.get("value_dim", 64).asInt();

        ValueCNN model(in_channels, input_hidden_channels, num_residual_blocks,
            policy_hidden_channels, value_dim);

        // 加载模型权重
        std::string model_path = config_json["infer"].get("model_path", "").asString();
        if (model_path.empty()) {
            throw std::runtime_error("模型路径未指定");
        }
        try {
            torch::load(model, model_path);
            std::cout << "模型权重已加载: " << model_path << '\n';
        }
        catch (const c10::Error& e) {
            throw std::runtime_error("加载模型权重失败: " + std::string(e.what()));
        }
        model->eval(); // 切换到评估模式
        return model;
    }

    MCTSParams getMCTSParams() {
        MCTSParams params;
        auto mcts_json = config_json["infer"]["mcts"];
        params.c_puct = mcts_json.get("c_puct", 0.8).asFloat();
        params.puct2 = mcts_json.get("puct2", 0.0).asFloat();
        params.noise_sigma = mcts_json.get("noise_sigma", 0.01).asFloat();
        params.train_simulation = mcts_json.get("train_simulation", 30).asInt();
        params.update_strategy = mcts_json.get("update_strategy", "max").asString();
        params.train_buff = mcts_json.get("train_buff", 0.8).asFloat();
        // 使用 0.8 防止兼容性问题
        return params;
    }

    // 获取 AI 执子方
    int getAIPlayer() {
        return config_json["infer"].get("ai_player", 1).asInt();
    }

private:
    Json::Value config_json;
};

// 返回值：成功：输入坐标；失败：返回 <-1,-1>
std::pair<int, int> readMove(UltimateTicTacToe& board) {
    std::cout << "请输入您的走子: (0索引)";
    std::string s;
    if (!std::getline(std::cin, s)) {
        std::cout << "输入错误，请重新输入\n";
        return std::make_pair(-1, -1);
    }

    int x, y;
    // 必须严格匹配左右括号、逗号、两个整数
    if (std::sscanf(s.c_str(), " (%d ,%d )", &x, &y) != 2) {
        std::cout << "输入错误，请重新输入\n";
        return std::make_pair(-1, -1);
    }
    if (!board.is_valid_move(x, y)) {
        std::cout << "输入位置不合法，请重新输入\n";
        return std::make_pair(-1, -1);
    }
    return std::make_pair(x, y);
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

    // 初始化模型，获取参数
    ValueCNN model = config.getModel();
    MCTSParams mctsParams = config.getMCTSParams();
    // 创建添加噪声使用的随机数引擎
    std::random_device rd;
    auto rand_engine = std::mt19937(rd());
    MCTS mcts(model, rand_engine, mctsParams);
    std::cout << "初始化完成\n";
    // 创建初始棋盘
    UltimateTicTacToe board;
    // 设置AI执子方
    int ai_player = config.getAIPlayer();
    std::cout << "AI执子方: " << ai_player << std::endl;
    std::cout << "对弈开始\n";
    // 输出空棋盘
    board.print_board();
    // 对弈循环
    while (!board.is_game_over()) {
        if (ai_player == board.get_current_player()) {
            // AI 走子
            std::cout << "AI 正在思考...\n";
            auto [value, probs, root_node] = std::get<std::tuple<float, std::vector<std::vector<float>>,
                std::shared_ptr<MCTSNode>>>(mcts.run(board, true));
            auto move = mcts.calc_next_move(root_node, probs, 0.0f);
            std::cout << "AI 走子: (" << move.first << ',' << move.second << ")\n";
            board.make_move(move);
        } else {
            std::pair<int, int> move = readMove(board);
            while (move.first == -1) {
                move = readMove(board);  // 输入错误，重新输入 
            }
            board.make_move(move);
        }
        board.print_board();
    }
    std::cout << "对弈结束，赢家为: " << board.get_winner() << std::endl;
    return 0;
}