// 读取配置文件，定义模型结构和预处理函数
#pragma once
#include <torch/torch.h>
#include "json5cpp.h"
#include "game.h"
#include <fstream>
#include <iostream>

// 定义残差块
struct ResidualBlockImpl : torch::nn::Module {
    torch::nn::Conv2d conv1;
    torch::nn::BatchNorm2d bn1;
    torch::nn::Conv2d conv2;
    torch::nn::BatchNorm2d bn2;
    torch::nn::ReLU relu;

    ResidualBlockImpl(int64_t channels);
    torch::Tensor forward(torch::Tensor x);
};
TORCH_MODULE(ResidualBlock);

// 定义模型结构
/*
    模型返回值说明：
    value：[-1, 1] 代表即将落子玩家的胜率，1代表必胜，-1代表必败
    probs： 每个位置的落子概率分布，概率越高代表该位置越有可能是即将落子玩家的最优落子位置

*/
struct ValueCNNImpl : torch::nn::Module {
    // 初始化层
    torch::nn::Conv2d conv_init; // 卷积层（输入输出长宽不变）
    torch::nn::BatchNorm2d bn_init; // 归一化 + 可学习仿射变换（y_i = gamma · x_i + beta）
    // 策略头
    torch::nn::Conv2d policy_conv1;
    torch::nn::BatchNorm2d policy_bn1;
    torch::nn::Conv2d policy_conv2;
    // 价值头
    torch::nn::Conv2d value_conv;
    torch::nn::BatchNorm2d value_bn;
    torch::nn::Linear value_fc1, value_fc2;
    // 残差块组
    torch::nn::ModuleList res_blocks;

    ValueCNNImpl(int in_channels, int input_hidden_channels, int num_residual_blocks, int policy_hidden_channels, int value_dim);

    // 前向传播
    std::pair<torch::Tensor, torch::Tensor> forward(torch::Tensor x);

    // 计算概率分布和局面价值，用于 MCTS 搜索
    // 函数直接返回CPU Tensor, 访问需要使用accessor
    std::pair<float, torch::Tensor> calc(torch::Tensor x);

    // 计算局面价值，用于生成有价值的随机初始局面
    // 推理设备统一在 train.cpp中指定
    // 这里不计算概率分布，节省计算资源
    float calc_value(torch::Tensor x);

};
TORCH_MODULE(ValueCNN);


// 创建自定义数据加载器
using MyExample = torch::data::Example<
        torch::Tensor,
        std::tuple<torch::Tensor,torch::Tensor,torch::Tensor>>;
struct MyStackTransform {
    using InputBatchType = std::vector<MyExample>;
    using OutputBatchType = MyExample;
    OutputBatchType apply_batch(const InputBatchType& batch);
};
class GameDataset : public torch::data::datasets::Dataset<GameDataset, MyExample> {
private:
    torch::Tensor boards;   // [N, C, H, W]
    torch::Tensor policies; // [N, H*W]
    torch::Tensor values;   // [N]
    torch::Tensor weights;  // [N]

public:
    GameDataset(torch::Tensor boards,
                torch::Tensor policies,
                torch::Tensor values,
                torch::Tensor weights);

    // 返回单个样本：{board[i], {policy[i], value[i], weight[i]}}
    MyExample get(size_t index) override;
    torch::optional<size_t> size() const override;
};
