#pragma once
#include "TreeNode.h"
#include "../games/base_game.h"
#include <algorithm>
#include <type_traits>
#include <random>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

struct SearchTreeNodeSnapshot {
    int id = -1;
    int parent_id = -1;
    int action = -1;
    int visits = 0;
    float q = 0.0f;
    float u = 0.0f;
    float prior = 0.0f;
    float value = 0.0f;
};

struct SearchTreeSnapshot {
    int selected_action = -1;
    int root_visits = 0;
    int node_count = 0;
    std::vector<SearchTreeNodeSnapshot> nodes;
};

template<typename Game, typename ActionList>
class MCTSPure {
    // An implementation of the Pure Monte Carlo Tree Search
    static_assert(std::is_base_of_v<GameBase<Game, ActionList>, Game>,
        "Template parameter 'Game' must inherit from 'GameBase' to make sure the interface is correct.");
private:
    int _n_playout;
    double _c_puct;
    bool _capture_search_tree;
    std::mt19937 rand_engine; // Random number generator engine, used to support multithreaded Generation
    std::shared_ptr<TreeNode<ActionList>> _root;
    SearchTreeSnapshot _last_search_tree;

    using Node = TreeNode<ActionList>;

    struct SnapshotEntry {
        std::shared_ptr<Node> node;
        std::shared_ptr<Node> parent;
        int action;
        std::vector<int> path;
    };

    SearchTreeSnapshot build_search_tree_snapshot(int selected_action) {
        SearchTreeSnapshot snapshot;
        snapshot.selected_action = selected_action;
        snapshot.root_visits = _root ? _root->_n_visits : 0;

        if (!_root) {
            return snapshot;
        }

        std::vector<SnapshotEntry> entries;
        std::vector<SnapshotEntry> pending;
        for (const auto& pair : _root->_children) {
            pending.push_back(SnapshotEntry{pair.second, _root, pair.first, {pair.first}});
        }

        while (!pending.empty()) {
            SnapshotEntry current = std::move(pending.back());
            pending.pop_back();
            entries.push_back(current);

            for (const auto& pair : current.node->_children) {
                std::vector<int> path = current.path;
                path.push_back(pair.first);
                pending.push_back(SnapshotEntry{pair.second, current.node, pair.first, std::move(path)});
            }
        }

        std::sort(entries.begin(), entries.end(), [](const SnapshotEntry& left, const SnapshotEntry& right) {
            if (left.node->_n_visits != right.node->_n_visits) {
                return left.node->_n_visits > right.node->_n_visits;
            }
            if (left.action != right.action) {
                return left.action < right.action;
            }
            return std::lexicographical_compare(
                left.path.begin(), left.path.end(), right.path.begin(), right.path.end());
        });

        std::unordered_map<const Node*, int> node_ids;
        node_ids.emplace(_root.get(), 0);
        for (size_t index = 0; index < entries.size(); ++index) {
            node_ids.emplace(entries[index].node.get(), static_cast<int>(index + 1));
        }

        auto make_node_snapshot = [this](
            const std::shared_ptr<Node>& node, int id, int parent_id, int action) {
            SearchTreeNodeSnapshot result;
            result.id = id;
            result.parent_id = parent_id;
            result.action = action;
            result.visits = node->_n_visits;
            result.q = node->_Q;
            result.prior = node->prior_p;
            result.value = node->get_value(static_cast<float>(_c_puct));
            result.u = node->_u;
            return result;
        };

        snapshot.nodes.push_back(make_node_snapshot(_root, 0, -1, -1));
        for (const SnapshotEntry& entry : entries) {
            const auto parent_id = node_ids.find(entry.parent.get());
            const int resolved_parent_id = parent_id == node_ids.end() ? -1 : parent_id->second;
            snapshot.nodes.push_back(make_node_snapshot(
                entry.node,
                node_ids.at(entry.node.get()),
                resolved_parent_id,
                entry.action));
        }
        snapshot.node_count = static_cast<int>(snapshot.nodes.size());
        return snapshot;
    }

public:
    // constructor
    // Inputs:
    //      c_puct: a number in (0, inf) that controls how quickly exploration
    //          converges to the maximum-value policy. A higher value means
    //          relying on the prior more.
    //      n_playout: the number of playouts each move performs in the search
    //      rand_engine: a random number generator engine.
    MCTSPure(
        int n_playout,
        float c_puct,
        const std::mt19937& rand_engine,
        bool capture_search_tree = false) :
        _n_playout(n_playout),
        _c_puct(c_puct),
        _capture_search_tree(capture_search_tree),
        rand_engine(rand_engine) {
        _root = std::make_shared<TreeNode<ActionList>>(nullptr, 1.0);
    }

    // Run a single playout from the root to the leaf, getting a value at
    // the leaf and propagating it back through its parents.
    // State is modified in - place, so a copy must be provided.
    void _playout(Game& state) {
        std::shared_ptr<TreeNode<ActionList>> node = _root;
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

        if (!end) {
            node->expand(state.get_valid_actions());
        }
        // Evaluate the leaf node by random rollout
        float leaf_value = _evaluate_rollout(state);
        // Update value and visit count of nodes in this traversal.
        node->update_recursive(-leaf_value);
    }

    // Use the rollout policy to play until the end of the game,
    // returning + 1 if the current player wins, -1 if the opponent wins,
    // and 0 if it is a tie.
    int _evaluate_rollout(Game& state, int limit = 300) {
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
        if (!end) {
            return 0;
        }
        if (winner == -1) { // tie
            return 0;
        } else {
            return (winner == player) ? 1 : -1;
        }
    }

    // Runs all playouts sequentially and returns the most visited action.
    //     state: the current game state
    //     Return : the selected action index
    int get_move(Game& state) {
        for (int n = 0; n < _n_playout; ++n) {
            Game state_copy = *(state.clone());
            _playout(state_copy);
        }

        if (_root->_children.empty()) {
            if (_capture_search_tree) {
                _last_search_tree = build_search_tree_snapshot(-1);
            } else {
                _last_search_tree = SearchTreeSnapshot{};
            }
            return -1;
        }

        auto best_child = std::max_element(
            _root->_children.begin(),
            _root->_children.end(),
            [](const auto& a, const auto& b) {
                return a.second->_n_visits < b.second->_n_visits;
            }
        );
        int best_move = best_child->first;

        // Capture the complete tree before moving root to the selected child.
        if (_capture_search_tree) {
            _last_search_tree = build_search_tree_snapshot(best_move);
        } else {
            _last_search_tree = SearchTreeSnapshot{};
        }
        update_with_move(best_move); // reuse the subtree for the chosen move

        return best_move;
    }

    SearchTreeSnapshot get_last_search_tree() const {
        return _last_search_tree;
    }

    // Step forward in the tree, keeping everything we already know
    //     about the subtree.
    void update_with_move(int last_move) {
        if (last_move == -1) {
            _root = std::make_shared<TreeNode<ActionList>>(nullptr, 1.0);
            return;
        } else {
            if (_root->_children.find(last_move) != _root->_children.end()) {
                _root = _root->_children[last_move];
                _root->_parent.reset();
            } else {
                _root = std::make_shared<TreeNode<ActionList>>(nullptr, 1.0);
            }
        }
    }

};
