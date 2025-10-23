#include "json5cpp.h"
#include <torch/torch.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include "network.hpp"
#include "utt.h"

// 生成参数结构体
struct GenParams
{
    int   num_samples;
    float empty_ratio;
    int   select_iteration_num;
    int   num_moves_max;
};
// 训练参数结构体
struct TrainParams {
    int   turns; // 训练轮数
    int   batch_size;
    int   num_epochs;
    float learning_rate;
    float train_data_ratio; // 训练集比例
    int patience; // x 个 epoch val_loss没有提升时，停止训练
    int num_workers; // 数据生成线程数
    torch::Device device; // 训练设备
};

// 存储测试棋盘状态的结构体
struct TestBoard {
    std::string out_id;
    torch::Tensor board_tensor;
};


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
    // 根据配置文件参数创建模型
    ValueCNN createModel() {
        int in_channels = IN_CHANNELS;
        int input_hidden_channels = config_json["model"]["input_hidden_channels"].asInt();
        int num_residual_blocks = config_json["model"]["num_residual_blocks"].asInt();
        int policy_hidden_channels = config_json["model"]["policy_hidden_channels"].asInt();
        int value_dim = config_json["model"]["value_dim"].asInt();
        auto model = ValueCNN(in_channels, input_hidden_channels, num_residual_blocks, policy_hidden_channels, value_dim);
        return model;
    };
    // 获取生成对弈数据时的参数
    GenParams getGenParams() {
        return GenParams{ config_json["train"]["generation"]["num_samples"].asInt(),
                        config_json["train"]["generation"]["empty_ratio"].asFloat(),
                        config_json["train"]["generation"]["select_iteration_num"].asInt(),
                        config_json["train"]["generation"]["num_moves_max"].asInt() };
    };
    // 获取 MCTS 参数
    MCTSParams getMCTSParams() {
        return MCTSParams{ config_json["train"]["mcts"]["c_puct"].asFloat(),
                        config_json["train"]["mcts"]["puct2"].asFloat(),
                        config_json["train"]["mcts"]["noise_sigma"].asFloat(),
                        config_json["train"]["mcts"]["train_simulation"].asInt(),
                        config_json["train"]["mcts"]["update_strategy"].asString(),
                        config_json["train"]["mcts"]["train_buff"].asFloat(), };
    };
    // 获取训练参数
    TrainParams getTrainParams() {
        // 解析设备选项
        std::string device_str = config_json["train"]["device"].asString();
        torch::Device device = torch::kCPU;
        if (device_str == "default") {
            device = torch::cuda::is_available() ? torch::kCUDA : torch::kCPU;
        } else if (device_str == "cpu") {
            device = torch::kCPU;
        } else if (device_str == "gpu") {
            device = torch::cuda::is_available() ? torch::kCUDA : torch::kCPU;
        } else {
            throw std::runtime_error("无效的设备选项: " + device_str);
        }
        return TrainParams{ config_json["train"]["turns"].asInt(),
                        config_json["train"]["batch_size"].asInt(),
                        config_json["train"]["num_epochs"].asInt(),
                        config_json["train"]["learning_rate"].asFloat(),
                        config_json["train"]["train_data_ratio"].asFloat(),
                        config_json["train"]["patience"].asInt(),
                        config_json["train"]["num_workers"].asInt(), device };
    };

    // 获取模型保存的绝对路径
    std::string getSavePath() {
        std::string folder_name = config_json["train"]["save_path"].asString();
        // 返回模型保存的绝对路径
        std::filesystem::path current_path = std::filesystem::current_path();
        std::filesystem::path absolute_path = current_path / folder_name; // 路径拼接
        return absolute_path.string();
    };

    // 获取测试输出的棋盘状态
    std::vector<TestBoard> getTestBoards() {
        Json::Value debug_json = config_json["debug"];
        // 遍历 debug_json, 得到输出
        std::vector<TestBoard> test_boards;
        for (const auto& item : debug_json) {
            TestBoard test_board;
            test_board.out_id = item["out_id"].asString();
            Json::Value board_json = item["board"];

            // 解析棋盘状态
            std::vector<std::vector<int>> board_vector;
            for (int i = 0; i < BOARD_SIZE; ++i) {
                std::vector<int> row;
                for (int j = 0; j < BOARD_SIZE; ++j) {
                    row.push_back(board_json[i][j].asInt());
                }
                board_vector.push_back(row);
            }

            NewGameParameters params{ board_vector, current_player };
            UltimateTicTacToe board = UltimateTicTacToe(params);
            test_board.board_tensor = board_to_tensor(board);
            test_boards.push_back(test_board);
        }
        return test_boards;
    }

private:
    Json::Value config_json;
};