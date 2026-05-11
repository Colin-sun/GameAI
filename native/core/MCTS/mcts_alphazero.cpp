#include "mcts_alphazero.h"
#include <algorithm>
#include <limits>
#include <numeric>
#include <cmath>

std::vector<float> softmax(const std::vector<float>& x) {
    float max_x = *std::max_element(x.begin(), x.end());
    std::vector<float> exp_x(x.size());
    for (auto x_i : x) {
        exp_x.push_back(std::exp(x_i - max_x)); // prevent overflow
    }
    float sum_exp_x = std::accumulate(exp_x.begin(), exp_x.end(), 0.0);
    std::vector<float> softmax_x(exp_x.size());
    std::transform(exp_x.begin(), exp_x.end(), softmax_x.begin(), [sum_exp_x](float x_i) {
        return x_i / sum_exp_x;
        });
    return softmax_x;
}

std::vector<float> add_dirichlet_noise(const std::vector<float> probs, std::mt19937& rand_engine) {
    // using hard_coded values now
    // p = 0.75 * probs + 0.25 * dirichlet_noise
    std::gamma_distribution<float> gamma(0.3, 1.0);
    int prob_length = probs.size();
    std::vector<float> noises(prob_length);
    for (int i = 0; i < prob_length; ++i) {
        noises[i] = gamma(rand_engine);
    }
    float noise_sum = std::accumulate(noises.begin(), noises.end(), 0.0);
    std::transform(noises.begin(), noises.end(), noises.begin(), [noise_sum](float noise_i) {
        return noise_i / noise_sum;
        });

    std::vector<float> probs_with_noise(prob_length);
    std::transform(probs.begin(), probs.end(), noises.begin(), probs_with_noise.begin(), [](float prob_i, float noise_i) {
        return prob_i * 0.75 + noise_i * 0.25;
        });
    return probs_with_noise;

}

template<typename Game>
MCTS_AlphaZero<Game>::MCTS_AlphaZero(int n_playout, float c_puct, const std::mt19937& rand_engine) :
    _n_playout(n_playout), _c_puct(c_puct), rand_engine(rand_engine) {
    _root = std::make_shared<TreeNode>(nullptr, 1.0);
}

template<typename Game>
void MCTS_AlphaZero<Game>::_playout(Game& state) {
    std::shared_ptr<TreeNode> node = _root;
    while (true) {
        if (node->is_leaf()) {
            break;
        }
        // Greedily select next move.
        auto action_node = node->select(_c_puct);
        int action = action_node.first;
        node = action_node.second;
        state.make_move(action);
    }

    const auto& [actions, probs, nn_value] = state.policy_value_fn();
    float leaf_value = nn_value;

    // Check for end of game
    auto game_end_result = state.get_done_winner();
    bool end = game_end_result.first;
    int winner = game_end_result.second;

    if (!end) {
        node->expand(actions, probs);
    } else {
        // for end state，return the "true" leaf_value
        if (winner == -1) {
            leaf_value = 0.0;
        } else if (winner == state.get_current_player()) {
            leaf_value = 1.0;
        } else {
            leaf_value = -1.0;
        }
    }
    // Update value and visit count of nodes in this traversal.
    node->update_recursive(-leaf_value);
}

template<typename Game>
std::pair<std::vector<int>, std::vector<float>> MCTS_AlphaZero<Game>::get_move_prob(Game& state, float temp) {
    for (int i = 0; i < _n_playout; i++) {
        Game state_copy = *(state.clone());
        _playout(state_copy);
    }

    // calc the move probabilities based on visit counts at the root node
    constexpr float kEpsilon = 1e-10; // prevent log(0)
    std::vector<int> actions;
    std::vector<float> n_visits;
    for (auto& child : _root->_children) {
        actions.push_back(child.first);
        n_visits.push_back(static_cast<float>(child.second->_n_visits) + kEpsilon);
    }
    for (int i = 0; i < n_visits.size(); i++) {
        n_visits[i] = std::log(n_visits[i]) / temp;
    }
    std::vector<float> probs = softmax(n_visits);
    return std::make_pair(actions, probs);
}

template<typename Game>
void MCTS_AlphaZero<Game>::update_with_move(int last_move) {
    if (last_move == -1) {
        _root = std::make_shared<TreeNode>(nullptr, 1.0);
        return;
    } else {
        if (_root->_children.find(last_move) != _root->_children.end()) {
            _root = _root->_children[last_move];
            _root->_parent = nullptr;
        } else {
            _root = std::make_shared<TreeNode>(nullptr, 1.0);
        }
    }
}

template<typename Game>
RunReturn MCTS_AlphaZero<Game>::get_move(Game& state, float temp, bool return_prob, bool is_selfplay) {
    std::vector<int> valid_actions = state.get_valid_actions();
    const auto& [actions, probs] = get_move_prob(state);
    int action;
    if (is_selfplay) {
        // add Dirichlet Noise for exploration(needed for self - play training)
        auto choose_probs = add_dirichlet_noise(probs, rand_engine);
        std::discrete_distribution<int> choose_distribution(choose_probs.begin(), choose_probs.end());
        action = actions[choose_distribution(rand_engine)];
        // update the root node and reuse the search tree
        update_with_move(action);
    } else {
        // with the default temp = 1e-3, it is almost equivalent
        // to choosing the move with the highest prob
        std::discrete_distribution<int> choose_distribution(probs.begin(), probs.end());
        action = actions[choose_distribution(rand_engine)];
        // reset the root node
        update_with_move(-1);
    }
    if (return_prob) {
        return std::make_pair(action, probs);
    } else {
        return action;
    }
}
