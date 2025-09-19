#include "network.hpp"

// 残差块实现
ResidualBlockImpl::ResidualBlockImpl(int64_t channels) :
    conv1(torch::nn::Conv2dOptions(channels, channels, 3).padding(1)),
    bn1(channels),
    conv2(torch::nn::Conv2dOptions(channels, channels, 3).padding(1)),
    bn2(channels),
    relu() {
    register_module("conv1", conv1);
    register_module("bn1", bn1);
    register_module("conv2", conv2);
    register_module("bn2", bn2);
    register_module("relu", relu);
}

torch::Tensor ResidualBlockImpl::forward(torch::Tensor x) {
    auto residual = x;
    auto out = relu(bn1(conv1(x)));
    out = bn2(conv2(out));
    out += residual;
    out = relu(out);
    return out;
}

// 模型结构实现
ValueCNNImpl::ValueCNNImpl(int in_channels, int input_hidden_channels, int num_residual_blocks,
    int policy_hidden_channels, int value_dim) :
    // 卷积层
    conv_init(register_module("conv_init",
        torch::nn::Conv2d(torch::nn::Conv2dOptions(in_channels, input_hidden_channels, 3).padding(1)))),
    // 归一化
    bn_init(register_module("bn_init",
        torch::nn::BatchNorm2d(input_hidden_channels))),
    // 策略头        
    policy_conv1(register_module("policy_conv1",
        torch::nn::Conv2d(torch::nn::Conv2dOptions(input_hidden_channels, policy_hidden_channels, 3).padding(1)))),
    policy_bn1(register_module("policy_bn1",
        torch::nn::BatchNorm2d(policy_hidden_channels))),
    policy_conv2(register_module("policy_conv2",
        torch::nn::Conv2d(torch::nn::Conv2dOptions(policy_hidden_channels, 1, 3).padding(1)))),
    // 价值头
    value_conv(register_module("value_conv",
        torch::nn::Conv2d(torch::nn::Conv2dOptions(input_hidden_channels, 1, 1)))),
    value_bn(register_module("value_bn",
        torch::nn::BatchNorm2d(1))),
    value_fc1(register_module("value_fc1",
        torch::nn::Linear(BOARD_SIZE* BOARD_SIZE, value_dim))),
    value_fc2(register_module("value_fc2",
        torch::nn::Linear(value_dim, 1))) {

    // 残差块组
    for (int64_t i = 0; i < num_residual_blocks; ++i) {
        res_blocks->push_back(ResidualBlock(input_hidden_channels));
    }
}

// 前向传播
std::pair<torch::Tensor, torch::Tensor> ValueCNNImpl::forward(torch::Tensor x) {
    // 初始卷积
    x = torch::relu(bn_init(conv_init(x)));

    // 残差块
    for (const auto& block : *res_blocks) {
        block->to(x.device()); // 将 block 转移到 x 所在设备
        x = block->as<ResidualBlock>()->forward(x);
    }

    // 策略头
    auto policy = torch::relu(policy_bn1(policy_conv1(x)));
    policy = policy_conv2(policy);
    policy = policy.squeeze(1);  // 移除通道维度
    auto policy_logits = policy.view({ x.size(0), -1 });

    // 价值头
    auto value = torch::relu(value_bn(value_conv(x)));
    value = value.view({ x.size(0), -1 });
    value = torch::relu(value_fc1(value));
    value = torch::tanh(value_fc2(value));

    return { value, policy_logits };
}

// 计算概率分布和局面价值，用于 MCTS 搜索
// 函数直接返回CPU Tensor, 访问需要使用accessor
// 函数与训练时的前向传播无关，仅用于生成训练数据
std::pair<float, torch::Tensor> ValueCNNImpl::calc(torch::Tensor x) {
    torch::NoGradGuard no_grad;
    eval();
    this->to(x.device());
    x = x.unsqueeze(0); // 将 x 变为 4D Tensor 来符合模型输入要求
    auto [value, logits] = forward(x);
    auto probs = torch::softmax(logits, 1).view({ -1, BOARD_SIZE, BOARD_SIZE });
    auto probs_return = probs.cpu().contiguous().view({ BOARD_SIZE, BOARD_SIZE });
    return { value.item<float>(), probs_return };
}

// 计算局面价值，用于生成有价值的随机初始局面
// 推理设备统一在 train.cpp中指定
// 这里不计算概率分布，节省计算资源
// 函数与训练时的前向传播无关，仅用于生成训练数据
float ValueCNNImpl::calc_value(torch::Tensor x) {
    torch::NoGradGuard no_grad;
    eval();
    this->to(x.device());
    x = x.unsqueeze(0); // 将 x 变为 4D Tensor 来符合模型输入要求
    x = torch::relu(bn_init(conv_init(x)));
    for (const auto& block : *res_blocks) {
        block->to(x.device()); // 将 block 转移到 x 所在设备
        x = block->as<ResidualBlock>()->forward(x);
    }
    auto value = torch::relu(value_bn(value_conv(x)));
    value = value.view({ x.size(0), -1 });
    value = torch::relu(value_fc1(value));
    value = torch::tanh(value_fc2(value));
    return value.item<float>();
}

// 棋盘状态转张量
torch::Tensor board_to_tensor(UltimateTicTacToe& current_game) {
    auto options = torch::TensorOptions().dtype(torch::kFloat32);

    // 获取棋盘状态
    auto board = current_game.get_board();
    auto meta_board = current_game.get_meta_board();
    auto current_player = current_game.get_current_player();
    if (board.empty() || meta_board.empty()) {
        throw std::runtime_error("Invalid board data");
    }

    // 初始化张量6个通道
    auto board_player = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);
    auto board_opponent = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);
    auto board_empty = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);

    // 这里额外放大了向量的维数
    auto meta_board_player = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);
    auto meta_board_opponent = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);
    auto meta_board_empty = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);

    // 填充每个通道的值
    for (int i = 0; i < BOARD_SIZE; ++i) {
        for (int j = 0; j < BOARD_SIZE; ++j) {
            board_player[i][j] = (board[i][j] == current_player) ? 1.0f : 0.0f;
            board_opponent[i][j] = (board[i][j] == 3 - current_player) ? 1.0f : 0.0f;
            board_empty[i][j] = (board[i][j] == 0) ? 1.0f : 0.0f;
        }
    }

    for (int i = 0; i < META_BOARD_SIZE; ++i) {
        for (int j = 0; j < META_BOARD_SIZE; ++j) {
            meta_board_player[i][j] = (meta_board[i][j] == current_player) ? 1.0f : 0.0f;
            meta_board_opponent[i][j] = (meta_board[i][j] == 3 - current_player) ? 1.0f : 0.0f;
            meta_board_empty[i][j] = (meta_board[i][j] == 0) ? 1.0f : 0.0f;
        }
    }

    // 堆叠成6通道的张量
    return torch::stack({
        board_player, board_opponent, board_empty,
        meta_board_player, meta_board_opponent, meta_board_empty
        }, 0);
}

// 数据加载器实现
MyStackTransform::OutputBatchType MyStackTransform::apply_batch(const InputBatchType& batch) {
    std::vector<torch::Tensor> data, policies, values, weights;
    for (const auto& ex : batch) {
        data.push_back(ex.data);
        auto [p, v, w] = ex.target;
        policies.push_back(p);
        values.push_back(v);
        weights.push_back(w);
    }
    return {
        torch::stack(data),
        std::make_tuple(torch::stack(policies), torch::stack(values), torch::stack(weights))
    };
}

GameDataset::GameDataset(torch::Tensor boards, torch::Tensor policies, torch::Tensor values, torch::Tensor weights) :
    boards(boards), policies(policies), values(values), weights(weights) {
}

MyExample GameDataset::get(size_t index) {
    return {
        boards[index],
        std::make_tuple(policies[index], values[index].unsqueeze(0), weights[index].unsqueeze(0))
    };
}

torch::optional<size_t> GameDataset::size() const {
    return values.size(0);
}

