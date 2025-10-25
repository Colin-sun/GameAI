#pragma once
#include "TreeNode.h"
#include "../base_game.h"
#include <random>
#include <memory>

template<typename Game, typename ActionList>
class MCTSPure {
    // An implementation of the Pure Monte Carlo Tree Search
    static_assert(std::is_base_of_v<GameBase<Game, ActionList>, Game>,
        "Template parameter 'Game' must inherit from 'GameBase' to make sure the interface is correct.");
private:
    int _n_playout;
    double _c_puct;
    std::mt19937 rand_engine; // Random number generator engine, used to support multithreaded Generation
    std::shared_ptr<TreeNode<ActionList>> _root;
public:
    // constructor
    // Inputs:
    //      c_puct: a number in (0, inf) that controls how quickly exploration
    //          converges to the maximum-value policy. A higher value means
    //          relying on the prior more.
    //      n_playout: the number of playouts each move performs in the search
    //      rand_engine: a random number generator engine.
    MCTSPure(int n_playout, float c_puct, const std::mt19937& rand_engine) :
        _n_playout(n_playout), _c_puct(c_puct), rand_engine(rand_engine) {
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

    // Step forward in the tree, keeping everything we already know
    //     about the subtree.
    void update_with_move(int last_move) {
        if (last_move == -1) {
            _root = std::make_shared<TreeNode<ActionList>>(nullptr, 1.0);
            return;
        } else {
            if (_root->_children.find(last_move) != _root->_children.end()) {
                _root = _root->_children[last_move];
                _root->_parent = nullptr;
            } else {
                _root = std::make_shared<TreeNode<ActionList>>(nullptr, 1.0);
            }
        }
    }

};
