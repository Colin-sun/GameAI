#include "mcts_pure.h"
#include <algorithm>
#include <limits>

template<typename Game>
MCTSPure<Game>::MCTSPure(int n_playout, float c_puct, const std::mt19937& rand_engine) :
    _n_playout(n_playout), _c_puct(c_puct), rand_engine(rand_engine) {
    _root = std::make_shared<TreeNode>(nullptr, 1.0);
}

template<typename Game>
void MCTSPure<Game>::_playout(Game& state) {

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

    // Check for end of game
    auto game_end_result = state.get_done_winner();
    bool end = game_end_result.first;
    int winner = game_end_result.second;

    if (!end) {
        node->expand(state.get_valid_actions());
    }
    // Evaluate the leaf node by random rollout
    float leaf_value = _evaluate_rollout(state);
    // Update value and visit count of nodes in this traversal.
    node->update_recursive(-leaf_value);
}

template<typename Game>
int MCTSPure<Game>::_evaluate_rollout(Game& state, int limit) {
    int player = state.get_current_player();
    bool end = false;
    int winner = -1;
    for (int i = 0; i < limit; ++i) {
        auto game_end_result = state.get_done_winner();
        end = game_end_result.first;
        winner = game_end_result.second;
        if (end) {
            break;
        }
        auto valid_moves = state.get_valid_actions();
        // use rand_engine to randomly select an action
        int action = valid_moves[rand_engine() % valid_moves.size()];
        state.make_move(action);
    }
    if (winner == -1) { // tie
        return 0;
    } else {
        return (winner == player) ? 1 : -1;
    }
}

template<typename Game>
int MCTSPure<Game>::get_move(Game& state) {
    for (int n = 0; n < _n_playout; ++n) {
        Game state_copy = *(state.clone());
        _playout(state_copy);
    }

    if (_root->_children.empty()) {
        return -1;
    }

    auto best_child = std::max_element(
        _root->_children.begin(),
        _root->_children.end(),
        [](const auto& a, const auto& b) {
            return a.second->_n_visits < b.second->_n_visits;
        }
    );

    update_with_move(-1); // clear MCTS

    return best_child->first;
}

template<typename Game>
void MCTSPure<Game>::update_with_move(int last_move) {
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
