// 测试模型输出
#include <torch/torch.h>
#include "json5cpp.h"
#include "game.h"
#include "minimax.h"
#include "network.hpp"

#include <fstream>
#include <iostream>
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

    UltimateTicTacToe getTestBoard() {
        Json::Value board_json = config_json["test"]["board"];
        std::vector<std::vector<int>> board;
        for (int i = 0; i < 9; ++i) {
            std::vector<int> row;
            for (int j = 0; j < 9; ++j) {
                row.push_back(board_json[i][j].asInt());
            }
            board.push_back(row);
        }
        std::pair<int, int> next_board = std::make_pair(config_json["test"]["next_board"][0].asInt(),
            config_json["test"]["next_board"][1].asInt());
        int current_player = config_json["test"].get("current_player", 1).asInt();
        return UltimateTicTacToe(board, next_board, current_player);
    }
private:
    Json::Value config_json;
};


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

    std::cout << "初始化完成\n";
    // 创建测试棋盘
    UltimateTicTacToe test_board = config.getTestBoard();
    auto test_tensor = board_to_tensor(test_board);
    float value = model->calc_value(test_tensor);
    std::cout << "测试棋盘的评估值为: " << value << '\n';
    return 0;
}