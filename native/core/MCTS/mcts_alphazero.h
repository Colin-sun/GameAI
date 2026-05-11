#pragma once
#include <random>
#include <memory>   
#include <vector>
#include "base_game.h"
#include "TreeNode.h"
#include <variant>
#include <type_traits> // for type check

using RunReturn = std::variant<int, std::pair<int, std::vector<float>>>;

template<typename Game>
class MCTS_AlphaZero {
    // An implementation of the Monte Carlo Tree Search with NN
    static_assert(std::is_base_of_v<GameBase, Game>,
        "Template parameter 'Game' must inherit from 'GameBase' to make sure the interface is correct.");
private:
    int _n_playout;
    float _c_puct;
    std::mt19937 rand_engine; // Random number generator engine, used to support multithreaded Generation
    std::shared_ptr<TreeNode> _root;
public:
    // constructor
    // Inputs:
    //      c_puct: a number in (0, inf) that controls how quickly exploration
    //          converges to the maximum-value policy. A higher value means
    //          relying on the prior more.
    //      n_playout: the number of playouts each move performs in the search
    //      rand_engine: a random number generator engine.
    // You should implement the policy_value_fn in your Game implementation
    MCTS_AlphaZero(int n_playout, float c_puct, const std::mt19937& rand_engine);

    // Run a single playout from the root to the leaf, getting a value at
    // the leaf and propagating it back through its parents.
    // State is modified in - place, so a copy must be provided.
    void _playout(Game& state);

    // Run all playouts sequentially and return the available actions and
    // their corresponding probabilities.
    // Input:
    //     state: the current game state
    //     temp : temperature parameter in(0, 1] controls the level of exploration
    // output:
    //     a list of tuples of action and probability corresponding to the
    //     available actions of the current state
    std::pair<std::vector<int>, std::vector<float>> get_move_prob(Game& state, float temp = 1e-3);

    // Step forward in the tree, keeping everything we already know
    //     about the subtree.
    void update_with_move(int last_move);

    // Runs all playouts sequentially and returns the action.
    //     state: the current game state
    //     temp : temperature parameter in(0, 1] controls the level of exploration)
    //     return_prob : whether to return the probabilities for each action
    //     is_selfplay : if true, then add Dirichlet Noise for exploration when choosing an action
    //     Return : the selected action
    RunReturn get_move(Game& state, float temp = 1e-3, bool return_prob = false, bool is_selfplay = false);
};