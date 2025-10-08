// 在原版井字棋上测试训练算法
#include <torch/torch.h>
#include "json5cpp.h"
#include "game.h"
#include "mcts.h"
#include "network.hpp"
#include <omp.h>
#include "omp_rng.hpp"
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <filesystem>

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
    int patience; // x个epoch val_loss没有提升时，停止训练
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
        // 模拟两次使用cd.. 的效果，返回此时模型保存的绝对路径
        std::filesystem::path current_path = std::filesystem::current_path();
        std::filesystem::path parent_path = current_path.parent_path().parent_path();
        std::filesystem::path absolute_path = parent_path / folder_name; // 路径拼接
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
            int current_player = item["current_player"].asInt();
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

// 学习率调度器类
class ReduceLROnPlateau {
public:
    ReduceLROnPlateau(torch::optim::Optimizer& optimizer,
        const std::string& mode = "min", // 当指标不再减小时降低学习率
        int patience = 2, // 容忍次数，连续2个epoch指标没有改善时触发学习率调整
        double factor = 0.5) // 学习率调整因子，新的学习率 = 旧的学习率 * 因子
        : optimizer_(optimizer), mode_(mode), patience_(patience), factor_(factor) {
        best_metric_ = (mode == "min") ? std::numeric_limits<double>::max()
            : std::numeric_limits<double>::lowest();
    }

    void step(double metric) {
        bool is_better = false;
        if (mode_ == "min") {
            is_better = metric < best_metric_;
        } else {
            is_better = metric > best_metric_;
        }

        if (is_better) {
            best_metric_ = metric;
            patience_counter_ = 0;
        } else {
            patience_counter_++;
            if (patience_counter_ >= patience_) {
                // 降低学习率
                reduce_lr();
                patience_counter_ = 0;
            }
        }
    }

private:
    void reduce_lr() {
        for (auto& param_group : optimizer_.param_groups()) {
            auto current_lr = param_group.options().get_lr();
            param_group.options().set_lr(current_lr * factor_);
        }
        std::cout << "学习率降低到: " <<
            optimizer_.param_groups()[0].options().get_lr() << std::endl;
    }

    torch::optim::Optimizer& optimizer_;
    std::string mode_;
    int patience_;
    int patience_counter_ = 0;
    double factor_;
    double best_metric_;
};

// 生成随机初始局面
UltimateTicTacToe generate_random_board(ValueCNN& model, std::mt19937& rand_engine, GenParams params) {
    // 随机数生成器初始化
    std::uniform_real_distribution<float> empty_distribution(0.0, 1.0);
    std::uniform_int_distribution<int> num_moves_distribution(1, params.num_moves_max);

    // 决定是否生成空局面
    float best_val = 1000000;
    auto best_board = UltimateTicTacToe();
    if (empty_distribution(rand_engine) < params.empty_ratio) {
        return best_board;
    } else {
        for (int i = 0; i < params.select_iteration_num; ++i) {
            // 随机走子生成局面
            int num_moves = num_moves_distribution(rand_engine);
            auto current_board = UltimateTicTacToe();
            for (int j = 0; j < num_moves; ++j) {
                auto vaild_moves = current_board.get_valid_moves();
                std::uniform_int_distribution<int> moves_distribution(0, vaild_moves.size() - 1);
                current_board.make_move(vaild_moves[moves_distribution(rand_engine)]);
                if (current_board.is_game_over()) {
                    break;
                }
            }

            // 判断局面好坏
            auto board_tensor = board_to_tensor(current_board);
            float value = model->calc_value(board_tensor);
            if (value < best_val) {
                best_val = value;
                best_board = current_board;
            }
        }
    }
    return best_board;
}

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

// 数据增强函数：实现 D4 对称变换（旋转和翻转）
std::tuple<std::vector<torch::Tensor>, std::vector<torch::Tensor>,
    std::vector<float>, std::vector<float>>
    augment_data(const std::vector<torch::Tensor>& boards,
        const std::vector<torch::Tensor>& policies,
        const std::vector<float>& values,
        const std::vector<float>& weights) {

    std::vector<torch::Tensor> augmented_boards, augmented_policies;
    std::vector<float> augmented_values, augmented_weights;

    // 对每组数据进行 D4变换（4种旋转 × 2种翻转 = 8种变换）
    for (size_t i = 0; i < boards.size(); ++i) {
        const auto& board = boards[i];
        const auto& policy = policies[i];
        float value = values[i];
        float weight = weights[i];

        // 4种旋转角度（0°, 90°, 180°, 270°）
        for (int k = 0; k < 4; ++k) {
            // 2种情况：不翻转和翻转
            for (int o = 0; o < 2; ++o) {
                // 对棋盘进行旋转
                auto new_board = board.clone();
                new_board = torch::rot90(new_board, k, { 1, 2 }); // 沿着维度1和2旋转

                // 对策略进行相同旋转
                auto new_policy = policy.clone();
                new_policy = torch::rot90(new_policy, k, { 0, 1 }); // 沿着维度0和1旋转

                // 如果需要翻转
                if (o == 1) {
                    new_board = torch::flip(new_board, { 2 });     // 沿维度2翻转
                    new_policy = torch::flip(new_policy, { 1 });   // 沿维度1翻转
                }

                // 添加增强后的数据
                augmented_boards.push_back(new_board);
                augmented_policies.push_back(new_policy);
                augmented_values.push_back(value);
                augmented_weights.push_back(weight);
            }
        }
    }

    return std::make_tuple(augmented_boards, augmented_policies,
        augmented_values, augmented_weights);
}

// 模型训练函数 
void train_model(ValueCNN& model,
    const std::vector<torch::Tensor>& train_boards,
    const std::vector<torch::Tensor>& train_policies,
    const std::vector<float>& train_values,
    const std::vector<float>& train_weights,
    const std::vector<torch::Tensor>& val_boards,
    const std::vector<torch::Tensor>& val_policies,
    const std::vector<float>& val_values,
    const std::vector<float>& val_weights,
    TrainParams train_params) {

    torch::set_num_threads(1);
    at::set_num_threads(1);

    int batchSize = train_params.batch_size;
    int num_epochs = train_params.num_epochs;
    float learning_rate = train_params.learning_rate;
    int num_workers = train_params.num_workers;

    // 创建数据集和数据加载器
    auto train_dataset = GameDataset({
        torch::stack(train_boards),
        torch::stack(train_policies),
        torch::tensor(train_values, torch::kFloat32),
        torch::tensor(train_weights, torch::kFloat32)
        }).map(MyStackTransform());

    auto val_dataset = GameDataset({
        torch::stack(val_boards),
        torch::stack(val_policies),
        torch::tensor(val_values, torch::kFloat32),
        torch::tensor(val_weights, torch::kFloat32)
        }).map(MyStackTransform());

    // 保存数据集大小
    size_t train_dataset_size = train_dataset.size().value();
    size_t val_dataset_size = val_dataset.size().value();

    auto train_loader = torch::data::make_data_loader<torch::data::samplers::RandomSampler>(
        std::move(train_dataset), torch::data::DataLoaderOptions().batch_size(batchSize).workers(num_workers));

    auto val_loader = torch::data::make_data_loader<torch::data::samplers::SequentialSampler>(
        std::move(val_dataset), torch::data::DataLoaderOptions().batch_size(batchSize).workers(num_workers));

    // 损失函数和优化器
    auto value_criterion = torch::nn::MSELoss(torch::nn::MSELossOptions().reduction(torch::kNone));
    auto policy_criterion = torch::nn::KLDivLoss(torch::nn::KLDivLossOptions().reduction(torch::kNone));
    auto val_value_criterion = torch::nn::MSELoss();
    auto val_policy_criterion = torch::nn::KLDivLoss(torch::nn::KLDivLossOptions().reduction(torch::kBatchMean));

    torch::optim::AdamW optimizer(model->parameters(),
        torch::optim::AdamWOptions(learning_rate).weight_decay(1e-4));
    // 创建学习率调度器
    ReduceLROnPlateau scheduler(optimizer, "min");
    auto device = train_params.device;
    std::cout << "开始训练，使用设备: " << device << "\n";
    model->to(device);

    // 记录最佳val_value_loss 和 val_policy_loss
    float best_val_value_loss = std::numeric_limits<float>::max();
    float best_val_policy_loss = std::numeric_limits<float>::max();
    int current_patience = 0;

    for (int epoch = 0; epoch < num_epochs; ++epoch) {
        // 训练阶段
        model->train();
        double train_value_loss = 0.0, train_policy_loss = 0.0;


        for (auto& batch : *train_loader) {
            auto batch_boards = batch.data.to(device);
            auto batch_policies = std::get<0>(batch.target).to(device);
            batch_policies = batch_policies.view({ batch_policies.size(0), -1 }); // 展平 [B,9,9] -> [B,81]
            auto batch_values = std::get<1>(batch.target).to(device); // [B,1]
            auto batch_weights = std::get<2>(batch.target).to(device); // [B,1]

            optimizer.zero_grad();

            auto outputs = model(batch_boards);
            auto pred_values = std::get<0>(outputs).to(device); // [B,1]
            auto pred_policies = std::get<1>(outputs).to(device); // [B,81]

            // 计算损失
            // 确保维度一致
            pred_values = pred_values.view({ -1 });          // [B,1] -> [B]
            batch_values = batch_values.view({ -1 });          // [B,1] -> [B]
            batch_weights = batch_weights.view({ -1 });        // [B,1] -> [B]

            batch_weights = batch_weights.detach();           // 权重不参与反向传播


            auto value_loss = value_criterion(pred_values, batch_values).view({ -1 }); // [B]
            auto policy_loss = policy_criterion(
                torch::log_softmax(pred_policies, 1),
                batch_policies.view({ -1, batch_policies.size(1) })
            ).mean(1); // [B]

            auto weighted_value_loss = (value_loss * batch_weights).mean();
            auto weighted_policy_loss = (policy_loss * batch_weights).mean();

            auto loss = 2 * weighted_value_loss + weighted_policy_loss;

            loss.backward();
            optimizer.step();
            double weighted_value_loss_val = weighted_value_loss.item<double>();
            double weighted_policy_loss_val = weighted_policy_loss.item<double>();

            train_value_loss += weighted_value_loss_val;
            train_policy_loss += weighted_policy_loss_val;
        }

        // 验证阶段
        model->eval();
        double val_value_loss = 0.0, val_policy_loss = 0.0;

        torch::NoGradGuard no_grad;
        for (auto& batch : *val_loader) {
            auto boards = batch.data.to(device);
            auto policies = std::get<0>(batch.target).to(device);
            policies = policies.view({ policies.size(0), -1 }); // 展平 [B,9,9] -> [B,81]
            auto values = std::get<1>(batch.target).to(device); // [B,1]
            auto weights = std::get<2>(batch.target).to(device); // [B,1]

            auto outputs = model(boards);
            auto pred_values = std::get<0>(outputs).to(device); // [B,1]
            auto pred_policies = std::get<1>(outputs).to(device); // [B,81]

            // 确保维度一致
            pred_values = pred_values.view({ -1 });          // [B,1] -> [B]
            values = values.view({ -1 });          // [B,1] -> [B]
            weights = weights.view({ -1 });        // [B,1] -> [B]

            double val_value_loss_tmp = val_value_criterion(pred_values, values).item<double>();
            double val_policy_loss_tmp = val_policy_criterion(
                torch::log_softmax(pred_policies, 1),
                policies.view({ -1, policies.size(1) })
            ).item<double>();

            // .item<double>() 直接取了损失函数的平均值
            val_value_loss += val_value_loss_tmp;
            val_policy_loss += val_policy_loss_tmp;
        }

        // 计算平均损失
        double avg_train_value = train_value_loss / train_dataset_size;
        double avg_train_policy = train_policy_loss / train_dataset_size;
        double avg_val_value = val_value_loss / val_dataset_size;
        double avg_val_policy = val_policy_loss / val_dataset_size;

        std::cout << "Epoch " << epoch + 1 << "/" << num_epochs << ":\n";
        std::cout << "  Train - Value Loss: " << avg_train_value
            << ", Policy Loss: " << avg_train_policy << "\n";
        std::cout << "  Val - Value Loss: " << avg_val_value
            << ", Policy Loss: " << avg_val_policy << "\n";

        // 早停逻辑
        if (avg_val_value < best_val_value_loss && avg_val_policy < best_val_policy_loss) {
            best_val_value_loss = avg_val_value;
            best_val_policy_loss = avg_val_policy;
            current_patience = 0;
        } else {
            ++current_patience;
            if (current_patience >= train_params.patience) {
                std::cout << "Early stopping at epoch " << epoch + 1 << ".\n";
                break;
            }
        }

        // 计算验证总损失
        double val_total_loss = 5 * avg_val_value + avg_val_policy;
        // 更新学习率
        scheduler.step(val_total_loss);
    }
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
    std::vector<TestBoard> test_boards = config.getTestBoards();
    torch::set_num_threads(1);
    at::set_num_threads(1);
    std::cout << "初始化完成\n";

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

        // 输出几个特定局面的评估值
        for (auto& test_board : test_boards) {
            float value = model->calc_value(test_board.board_tensor);
            std::cout << test_board.out_id << std::endl;
            std::cout << "局面评估值为: " << value << std::endl;
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