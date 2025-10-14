#ifndef MCTS_H
#define MCTS_H

#include <cmath>
#include <algorithm>
#include <torch/torch.h>
#include "game.h"
#include <string>
#include "network.hpp"
#include <random>
#include <memory>
#include <unordered_map>
#include <variant>
#include <numeric>
#include <algorithm>
#include <vector>

// 节点类前向声明
class MCTSNode;
// 声明 run 函数的返回类型
using RunReturn = std::variant<
    std::pair<float, std::vector<std::vector<float>>>,
    std::tuple<float, std::vector<std::vector<float>>, std::shared_ptr<MCTSNode>>
>;
using ResultsReturn = std::variant<
    std::pair<float, std::vector<std::vector<float>>>,
    std::tuple<float, std::vector<std::vector<float>>, int>
>;

// 用于哈希pair<int, int>
struct PairHash {
    std::size_t operator()(const std::pair<int, int>& p) const;
};

// 存储 MCTS 配置参数的结构体
struct MCTSParams
{
    float c_puct;
    float puct2;
    float noise_sigma;
    int   train_simulation;
    std::string update_strategy;
    float train_buff;
};

// MCTS类声明
class MCTS {
private:
    ValueCNN model;
    std::mt19937 rand_engine; // 添加噪声时使用
    // 探索性参数,详细定义见配置文件
    float c_puct;
    float puct2;
    float noise_sigma;
    bool is_train; // 是否为训练模式
    std::string update_strategy; // 节点值更新策略
    int train_simulation; // MCTS搜索次数
    float train_buff; // 训练样本权重
    std::vector<std::shared_ptr<MCTSNode>> visited_nodes; // 存储访问过的节点(仅训练时使用)

public:
    MCTS(ValueCNN& model, const std::mt19937& rand_engine, const MCTSParams& mctsParams, bool is_train = true);

    // 创建一个节点
    std::shared_ptr<MCTSNode> new_node(const UltimateTicTacToe& board, std::shared_ptr<MCTSNode> parent = nullptr,
        std::pair<int, int> move = std::make_pair(-1, -1));

    // 运行MCTS搜索
    RunReturn run(UltimateTicTacToe& root_board, bool return_root = false);

    // 选择子节点
    std::shared_ptr<MCTSNode> select_child(std::shared_ptr<MCTSNode> node);

    // 生成子节点
    void expand_node(std::shared_ptr<MCTSNode> node);

    // 使用原始评估函数评估节点
    float evaluate_node(std::shared_ptr<MCTSNode> node);

    /// @brief 从 MCTS节点中提取搜索结果
    /// @param root_node MCTS节点
    /// @param train 是否处于训练模式
    /// @param return_total_visits 是否返回总访问次数(便于训练数据生成)
    /// @return 一般返回值是一个pair，包含：根节点的估值，每个可能动作的概率分布
    /// return_total_visits 为 true 时返回总访问次数
    ResultsReturn get_results(std::shared_ptr<MCTSNode> root_node,
        bool train = false, bool return_total_visits = false);

    /// @brief 获取训练数据
    /// @return 返回值是一个tuple，包含：局面列表（Tensor），策略列表（Tensor），价值列表，权重列表
    std::tuple<std::vector<torch::Tensor>, std::vector<torch::Tensor>,
        std::vector<float>, std::vector<float>> get_train_data();
    // 计算下一步走法
    std::pair<int, int> calc_next_move(std::shared_ptr<MCTSNode> root_node, std::vector<std::vector<float>> probs,
        float temperature = 0.0f);

};

// 节点类
class MCTSNode {
public:
    UltimateTicTacToe board;
    std::shared_ptr<MCTSNode> parent; // 父节点
    std::pair<int, int> move;
    // 子节点索引表：< <父节点->子节点走哪一步>, <子节点指针，该步优劣的先验概率> >
    std::unordered_map<std::pair<int, int>, std::pair<std::shared_ptr<MCTSNode>, float>, PairHash> children;
    int visit_count;
    float value_sum;
    float val; // 节点估值
    float value; // 当前局面的好坏评估
    // 使用评估函数计算终局 value，使用神经网络估算非终局 value

    MCTSNode(UltimateTicTacToe board, std::shared_ptr<MCTSNode> parent, std::pair<int, int> move);
    void update_value(std::string update_stratigy);

    // 调用board的终局判断方法
    bool is_terminal();
    // 原始评估函数
    float evaluate();

};

#endif // MCTS_H
