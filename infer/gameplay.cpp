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

private:
    Json::Value config_json;
};

// 生成单局游戏数据
std::tuple<std::vector<torch::Tensor>, std::vector<torch::Tensor>,
    std::vector<float>, std::vector<float>> generate_single_game(ValueCNN& model, std::mt19937& rand_engine, MCTSParams mcts_params, GenParams params) {
    auto board = generate_random_board(model, rand_engine, params);
    MCTS mcts(model, rand_engine, mcts_params);
    // 随机温度
    std::uniform_real_distribution<float> temp_dist(0.1, 1.0);
    float temperature = temp_dist(rand_engine);
    while (!board.is_game_over()) {
        // MCTS获取策略
        auto [value, probs, root_node] = std::get<std::tuple<float, std::vector<std::vector<float>>,
            std::shared_ptr<MCTSNode>>>(mcts.run(board, true));
        board.make_move(mcts.calc_next_move(root_node, probs, temperature));
    }
    return mcts.get_train_data();
}

// 在主函数中使用训练函数
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
    ValueCNN model = config.createModel(); // 线程只读推理，不能写 thread_local
    GenParams genParams = config.getGenParams();
    MCTSParams mctsParams = config.getMCTSParams();
    TrainParams trainParams = config.getTrainParams();
    torch::set_num_threads(1);
    at::set_num_threads(1);
    std::cout << "模型初始化完成\n";

    // 生成训练数据
    // 声明最终的聚合容器
    std::vector<torch::Tensor> final_boards, final_policies;
    std::vector<float> final_values, final_weights;
    for (int i = 0; i < trainParams.turns; ++i) {
        model->to(torch::kCPU);
        // 并行生成对弈数据并在过程中直接增强数据
#pragma omp parallel for schedule(static) num_threads(trainParams.num_workers)
        for (int j = 0; j < genParams.num_samples; ++j) {
            // 每个线程构造自己的随机数生成器
            auto& rng = threaded_rng();

            // 生成单局游戏数据
            auto [boards, policies, values, weights] = generate_single_game(model, rng, mctsParams, genParams);

            // 直接进行数据增强
            auto [aug_boards, aug_policies, aug_values, aug_weights] =
                augment_data(boards, policies, values, weights);

            // 使用 critical section 安全地合并增强后的数据
#pragma omp critical
            {
                std::cout << "线程 " << omp_get_thread_num() << " 生成了 " << aug_boards.size() << " 个增强样本\n";
                final_boards.insert(final_boards.end(),
                    std::make_move_iterator(aug_boards.begin()),
                    std::make_move_iterator(aug_boards.end()));

                final_policies.insert(final_policies.end(),
                    std::make_move_iterator(aug_policies.begin()),
                    std::make_move_iterator(aug_policies.end()));

                final_values.insert(final_values.end(),
                    std::make_move_iterator(aug_values.begin()),
                    std::make_move_iterator(aug_values.end()));

                final_weights.insert(final_weights.end(),
                    std::make_move_iterator(aug_weights.begin()),
                    std::make_move_iterator(aug_weights.end()));
            }
        }

        // 转换为张量并训练
        if (!final_boards.empty()) {
            // 划分训练集和验证集
            float train_ratio = trainParams.train_data_ratio; // 从配置文件读取
            int num_train = static_cast<int>(final_boards.size() * train_ratio);

            // 生成训练集和验证集
            // 训练集
            std::vector<torch::Tensor> train_boards(final_boards.begin(), final_boards.begin() + num_train);
            std::vector<torch::Tensor> train_policies(final_policies.begin(), final_policies.begin() + num_train);
            std::vector<float> train_values(final_values.begin(), final_values.begin() + num_train);
            std::vector<float> train_weights(final_weights.begin(), final_weights.begin() + num_train);

            // 验证集
            std::vector<torch::Tensor> val_boards(final_boards.begin() + num_train, final_boards.end());
            std::vector<torch::Tensor> val_policies(final_policies.begin() + num_train, final_policies.end());
            std::vector<float> val_values(final_values.begin() + num_train, final_values.end());
            std::vector<float> val_weights(final_weights.begin() + num_train, final_weights.end());

            // 训练模型
            train_model(model,
                train_boards, train_policies, train_values, train_weights,
                val_boards, val_policies, val_values, val_weights, trainParams);

            // 清空数据为下一轮训练做准备
            final_boards.clear();
            final_policies.clear();
            final_values.clear();
            final_weights.clear();
        }

        // 保存模型checkpoint
        auto save_path = config.getSavePath();
        // 根据路径创建文件夹
        std::filesystem::create_directories(save_path);
        // 检查是否成功创建文件夹
        if (!std::filesystem::exists(save_path)) {
            std::cerr << "无法在指定路径保存模型: " << save_path << std::endl;
            return EXIT_FAILURE;
        }
        torch::save(model, save_path + "/model_turn" + std::to_string(i + 1) + ".pt");
        std::cout << "模型已保存到: " << save_path + "/model_turn" + std::to_string(i + 1) + ".pt" << std::endl;
    }
    return 0;
}