#include "mcts.h"

// 用于哈希pair<int, int>
std::size_t PairHash::operator()(const std::pair<int, int>& p) const {
    // 使用常见的pair哈希方法
    return std::hash<int>{}(p.first) ^ (std::hash<int>{}(p.second) << 1);
}


// 节点类
MCTSNode::MCTSNode(UltimateTicTacToe board, std::shared_ptr<MCTSNode> parent = nullptr, std::pair<int, int> move = std::make_pair(-1, -1))
    : board(board), parent(parent), move(move), visit_count(0), value_sum(0), val(0), value(0) {
}

void MCTSNode::update_value(std::string update_stratigy) {
    if (children.empty()) {
        val = value;
    } else {
        if (update_stratigy == "mean") {
            val = value_sum / visit_count;
        } else {
            float max_val = -10.0f;
            for (const auto& child_pair : children) {
                std::shared_ptr<MCTSNode> child = child_pair.second.first;
                if (child != nullptr) {
                    max_val = std::max(max_val, -child->val);
                }
            }
            val = (max_val == -10.0f) ? value : max_val;
        }
    }
}
// 调用board的终局判断方法
bool MCTSNode::is_terminal() {
    return board.is_game_over();
}
// 评估函数
// 参照原代码定义，0 为平局，负值为先手胜，正值为后手胜
// 同时鼓励 ai在更少的步数内获胜
float MCTSNode::evaluate(int max_player) {
    auto winner = board.get_winner();
    auto step = board.get_step();
    if (winner == 0) {
        return 0;
    } else if (winner == 3 - max_player) {
        return -(1 - step * 3e-4); // 鼓励更快获胜
    } else if (winner == max_player) {
        return 1 - step * 3e-4;
    } else {
        return 0 + step * 3e-4; // 鼓励更慢平局
    }
}


MCTS::MCTS(ValueCNN& model, const std::mt19937& rand_engine, const MCTSParams& mctsParams, bool is_train)
    : model(model),
    rand_engine(rand_engine),
    c_puct(mctsParams.c_puct),
    puct2(mctsParams.puct2),
    noise_sigma(mctsParams.noise_sigma),
    is_train(is_train),
    update_strategy(mctsParams.update_strategy),
    train_simulation(mctsParams.train_simulation),
    train_buff(mctsParams.train_buff) {
}

// 创建一个节点
std::shared_ptr<MCTSNode> MCTS::new_node(const UltimateTicTacToe& board, std::shared_ptr<MCTSNode> parent,
    std::pair<int, int> move) {
    std::shared_ptr<MCTSNode> node = std::make_shared<MCTSNode>(board, parent, move);
    if (is_train) {
        visited_nodes.push_back(node);
    }
    return node;
}

// 运行MCTS搜索
RunReturn MCTS::run(UltimateTicTacToe& root_board, bool return_root) {
    std::shared_ptr<MCTSNode> root_node = new_node(root_board);
    if (!root_node) {
        throw std::runtime_error("Failed to create root node.");
    }
    if (is_train) {
        visited_nodes.push_back(root_node);
    }
    // 预分配空间，避免频繁扩容
    // 一般 40 步内棋局结束
    std::vector<std::shared_ptr<MCTSNode>> search_path;
    search_path.reserve(50);
    int max_player = root_board.get_current_player();
    for (int i = 0; i < train_simulation; ++i) {
        std::shared_ptr<MCTSNode> current_node = root_node;
        search_path.clear();
        search_path.push_back(current_node);

        while (!current_node->children.empty()) {
            current_node = select_child(current_node);
            search_path.push_back(current_node);
        }

        if (!current_node->is_terminal()) {
            expand_node(current_node);
        } else {
            current_node->value = evaluate_node(current_node, max_player);
        }

        float value = evaluate_node(current_node, max_player);

        // 更新节点值
        for (auto it = search_path.rbegin(); it != search_path.rend(); ++it) {
            (*it)->visit_count += 1;
            (*it)->value_sum += value;
            (*it)->update_value(update_strategy);
            value = -value;
        }
    }

    if (!return_root) {
        auto results = get_results(root_node, is_train, false);
        if (std::holds_alternative<std::pair<float, std::vector<std::vector<float>>>>(results)) {
            return std::get<std::pair<float, std::vector<std::vector<float>>>>(results);
        } else {
            auto& tuple_results = std::get<std::tuple<float, std::vector<std::vector<float>>, int>>(results);
            return std::pair<float, std::vector<std::vector<float>>>(
                std::get<0>(tuple_results),
                std::get<1>(tuple_results)
            );
        }
    } else {
        auto results = get_results(root_node, is_train, false);
        if (std::holds_alternative<std::pair<float, std::vector<std::vector<float>>>>(results)) {
            auto& pair_results = std::get<std::pair<float, std::vector<std::vector<float>>>>(results);
            return std::tuple<float, std::vector<std::vector<float>>, std::shared_ptr<MCTSNode>>(
                pair_results.first,
                pair_results.second,
                root_node
            );
        } else {
            auto& tuple_results = std::get<std::tuple<float, std::vector<std::vector<float>>, int>>(results);
            return std::tuple<float, std::vector<std::vector<float>>, std::shared_ptr<MCTSNode>>(
                std::get<0>(tuple_results),
                std::get<1>(tuple_results),
                root_node
            );
        }
    }
}

// 选择子节点
std::shared_ptr<MCTSNode> MCTS::select_child(std::shared_ptr<MCTSNode> node) {
    // PUCT公式计算
    int total_visits = 0;
    for (const auto& child_pair : node->children) {
        std::shared_ptr<MCTSNode> child = child_pair.second.first;
        total_visits += (child != nullptr) ? child->visit_count : 0;
    }

    float explore_buff = std::pow(total_visits + 1, 0.5);
    float log_total = std::log(total_visits + 1);

    float best_score = -1e9;
    std::pair<int, int> best_move;

    float exp1 = 0, exp2 = 0;
    for (const auto& child_pair : node->children) {
        std::shared_ptr<MCTSNode> child = child_pair.second.first;
        if (child != nullptr) {
            exp1 += child->val * child->visit_count;
            exp2 += child->visit_count;
        }
    }
    float ave = exp1 / (exp2 + 1e-5); // 子节点访问情况的平均值

    for (const auto& child_pair : node->children) {
        std::pair<int, int> move = child_pair.first;
        std::shared_ptr<MCTSNode> child = child_pair.second.first;
        float prior = child_pair.second.second; // 先验概率，表示在当前状态下选择该动作的初始倾向

        float explore = c_puct * prior * explore_buff; // 探索项
        float exploit = ave;
        if (child != nullptr && child->visit_count != 0) {
            exploit = child->val;
            explore /= (child->visit_count + 1);
        }

        // 额外的探索机制
        explore += puct2 * std::sqrt(log_total / ((child != nullptr ? child->visit_count : 0) + 1));
        float score = explore - exploit;

        if (score > best_score) {
            best_score = score;
            best_move = move;
        }
    }

    // 为节省内存，生成子节点时指针初始设为 nullptr，待选择时再创建
    std::shared_ptr<MCTSNode> child_node = node->children[best_move].first;
    if (child_node == nullptr) {
        UltimateTicTacToe new_board = node->board;
        new_board.make_move(best_move);
        child_node = new_node(new_board, node, best_move);
        if (is_train) {
            visited_nodes.push_back(child_node);
        }
        node->children[best_move] = std::make_pair(child_node, node->children[best_move].second);
    }
    return child_node;
}

// 生成子节点
void MCTS::expand_node(std::shared_ptr<MCTSNode> node) {
    // 使用神经网络评估父节点，获取价值和策略
    auto result = model->calc(board_to_tensor(node->board));
    node->value = result.first;
    auto policy_tensor = result.second;
    auto policy = policy_tensor.accessor<float, 2>();

    // 策略归一化处理
    // 对于每个合法落子位置，累加其策略值到 policy_sum
    float sum_1 = 0;
    auto valid_moves = node->board.get_valid_moves();
    for (auto& move : valid_moves) {
        int i = move.first, j = move.second;
        sum_1 += policy[i][j];
    }
    // 避免除零
    if (sum_1 == 0) {
        sum_1 += 1e-10;
    }

    // 再次遍历，为每个合法落子位置创建子节点，同时添加噪声提升探索性
    // 为节省内存，子节点指针初始设为 nullptr，待选择时再创建
    std::normal_distribution<float> normal_distribution(0.0, noise_sigma);
    for (auto& move : valid_moves) {
        int i = move.first, j = move.second;
        node->children[move] = std::make_pair(nullptr, policy[i][j] / sum_1
            + normal_distribution(rand_engine));
    }

}

// 使用原始评估函数评估节点
// 逻辑：终局使用评估函数，非终局使用神经网络估值
float MCTS::evaluate_node(std::shared_ptr<MCTSNode> node, int max_player) {
    if (node->is_terminal()) {
        return node->evaluate(max_player);
    } else {
        return node->value;
    }
}

/// @brief 从 MCTS节点中提取搜索结果
/// @param root MCTS节点
/// @param train 是否处于训练模式
/// @param return_total_visits 是否返回总访问次数(便于训练数据生成)
/// @return 一般返回值是一个pair，包含：根节点的估值，每个可能动作的概率分布
/// return_total_visits 为 true 时同时返回总访问次数
ResultsReturn MCTS::get_results(std::shared_ptr<MCTSNode> root_node,
    bool train, bool return_total_visits) {
    // 创建概率二维向量
    std::vector<std::vector<float>> probs(BOARD_SIZE, std::vector<float>(BOARD_SIZE, 0));
    // 统计总访问次数
    int total_visits = 0;
    for (const auto& child_pair : root_node->children) {
        std::shared_ptr<MCTSNode> child = child_pair.second.first;
        total_visits += (child != nullptr) ? child->visit_count : 0; // 只有被访问的子节点才会被创建
    }

    if (!train) {
        // 非训练模式逻辑
        for (const auto& child_pair : root_node->children) {
            std::shared_ptr<MCTSNode> child = child_pair.second.first;
            // 计算动作概率并返回
            if (child != nullptr) {
                int i = child_pair.first.first, j = child_pair.first.second;
                probs[i][j] = (total_visits > 0) ? static_cast<float>(child->visit_count) / total_visits : 0;
            }
        }
        return std::make_pair(root_node->value_sum / root_node->visit_count, probs);
    } else {
        // 训练模式逻辑
        auto root_evaluate_result = model->calc(board_to_tensor(root_node->board));
        float root_value = root_evaluate_result.first;
        auto root_policy_tensor = root_evaluate_result.second;
        auto root_policy = root_policy_tensor.accessor<float, 2>();

        // 策略归一化
        float sum_vaild_prob = 1e-10f; // 避免除零，使用float字面量
        auto valid_moves = root_node->board.get_valid_moves();
        // 第一次遍历：计算有效概率之和
        for (auto& move : valid_moves) {
            int i = move.first, j = move.second;
            sum_vaild_prob += root_policy[i][j];
        }
        // 第二次遍历：归一化处理
        for (auto& move : valid_moves) {
            int i = move.first, j = move.second;
            probs[i][j] = root_policy[i][j] / sum_vaild_prob;
        }

        // 构建候选动作列表
        float sum_visited_prob = 0;
        // 包含 (<行,列>, 子节点指针)
        std::vector<std::pair<std::pair<int, int>, std::shared_ptr<MCTSNode>>> visited_moves_nodes;
        for (const auto& child_pair : root_node->children) {
            std::shared_ptr<MCTSNode> child = child_pair.second.first;
            // 收集所有被访问过的子节点
            if (child != nullptr && child->visit_count != 0) {
                sum_visited_prob += probs[child_pair.first.first][child_pair.first.second];
                visited_moves_nodes.emplace_back(child_pair.first, child);
                probs[child_pair.first.first][child_pair.first.second] = 0;
            }
        }

        // 按访问次数降序排序
        std::sort(visited_moves_nodes.begin(), visited_moves_nodes.end(), [](const auto& a, const auto& b) {
            return a.second->visit_count > b.second->visit_count;
            });

        // 重新分配概率
        float value_sum = 0;
        for (int i = 0; i < visited_moves_nodes.size(); ++i) {
            float cnt = visited_moves_nodes[i].second->visit_count;
            if (i + 1 < visited_moves_nodes.size()) {
                cnt -= visited_moves_nodes[i + 1].second->visit_count;
            }
            if (cnt == 0) {
                continue;
            }

            // 在当前考虑的节点范围内选择估值最高的节点
            float best_val = -1e9;
            int best_move_index = i;
            for (int j = 0; j <= i; ++j) {
                float current_val = -visited_moves_nodes[j].second->val;
                if (current_val > best_val) {
                    best_val = current_val;
                    best_move_index = j;
                }
            }

            probs[visited_moves_nodes[best_move_index].first.first][visited_moves_nodes[best_move_index].first.second]
                += sum_visited_prob * cnt * (i + 1) / total_visits;
            value_sum += best_val * cnt * (i + 1) / total_visits;
        }

        // 验证确保所有概率值非负
        for (int i = 0; i < BOARD_SIZE; ++i) {
            for (int j = 0; j < BOARD_SIZE; ++j) {
                if (probs[i][j] < 0) {
                    assert(false);
                }
            }
        }
        if (return_total_visits) {
            return std::make_tuple(root_value, probs, total_visits);
        } else {
            return std::make_pair(root_value, probs);
        }
    }
}

/// @brief 获取训练数据
/// @return 返回值是一个tuple，包含：局面列表（Tensor），策略列表（Tensor），价值列表，权重列表
std::tuple<std::vector<torch::Tensor>, std::vector<torch::Tensor>,
    std::vector<float>, std::vector<float>> MCTS::get_train_data() {
    std::vector<torch::Tensor> boards_tensor;
    std::vector<torch::Tensor> policies_tensor;
    std::vector<float> values;
    std::vector<float> weights;

    for (std::shared_ptr<MCTSNode> root : visited_nodes) {
        int child_count = 0;
        for (const auto& child_pair : root->children) {
            if (child_pair.second.first != nullptr) {
                child_count++;
            }
        }

        if (!(child_count > 1 || root->visit_count >= train_simulation / 2)) {
            continue;
        }

        // 获取结果
        auto results = get_results(root, true, true);
        if (std::holds_alternative<std::tuple<float, std::vector<std::vector<float>>, int>>(results)) {
            auto& tuple_results = std::get<std::tuple<float, std::vector<std::vector<float>>, int>>(results);
            float value_sum = std::get<0>(tuple_results);
            auto& probs = std::get<1>(tuple_results);
            int total_visits = std::get<2>(tuple_results);

            // 向量转换
            boards_tensor.push_back(board_to_tensor(root->board));
            auto options = torch::TensorOptions().dtype(torch::kFloat32);
            auto prob_tensor = torch::zeros({ BOARD_SIZE, BOARD_SIZE }, options);
            for (int i = 0; i < BOARD_SIZE; i++) {
                prob_tensor.slice(0, i, i + 1) = torch::from_blob(probs[i].data(), { BOARD_SIZE }, options).clone();
            }
            policies_tensor.push_back(prob_tensor);
            values.push_back(value_sum);
            auto weight_tmp = std::sqrt(static_cast<float>(total_visits) / train_simulation) * train_buff;
            weights.push_back(weight_tmp);
        } else {
            throw std::runtime_error("Invalid results type.");
        }
    }

    return std::make_tuple(boards_tensor, policies_tensor, values, weights);
};

// 计算下一步走法
std::pair<int, int> MCTS::calc_next_move(std::shared_ptr<MCTSNode> root_node, std::vector<std::vector<float>> probs, float temperature) {
    auto valid_moves = root_node->board.get_valid_moves();
    if (valid_moves.empty()) {
        // 防止访问非法内存
        throw std::runtime_error("No valid moves available.");
    }

    if (temperature == 0.0f) {
        // 找最大概率 move，无需排序
        float max_prob = -1.0f;
        std::pair<int, int> best_move;
        for (const auto& move : valid_moves) {
            float prob = probs[move.first][move.second];
            if (prob > max_prob) {
                max_prob = prob;
                best_move = move;
            }
        }
        return best_move;
    } else {
        std::vector<float> probs_vec;
        probs_vec.reserve(valid_moves.size()); // 预分配内存

        // 温度调整概率
        std::transform(valid_moves.begin(), valid_moves.end(), std::back_inserter(probs_vec),
            [temperature, &probs](const std::pair<int, int>& move) {
                return std::pow(probs[move.first][move.second], 1.0f / temperature);
            });

        // 归一化概率分布
        float sum = std::accumulate(probs_vec.begin(), probs_vec.end(), 0.0f);
        if (sum == 0.0f) {
            sum = 1e-10f; // 避免除零
        }
        std::transform(probs_vec.begin(), probs_vec.end(), probs_vec.begin(),
            [sum](float p) { return p / sum; });

        std::discrete_distribution<int> distribution(probs_vec.begin(), probs_vec.end());
        int move_index = distribution(rand_engine);
        return valid_moves[move_index];
    }
}

