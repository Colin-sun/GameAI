// define standard interfaces
#pragma once
#include <vector>
#include <functional>
#include <unordered_map>
#include <memory>
#include <random>

// Game class
// All game should inherit from this class and implement all the functions
template<typename Derived, typename ActionList>
class GameBase {
public:
    // get legal actions
    // action_index should be positive
    virtual const ActionList get_valid_actions() const = 0;

    // check if action is valid
    virtual bool is_action_valid(int action) const = 0;

    // make a move
    virtual bool make_move(int action) = 0;

    // get whether the game is done and winner
    // winner == -1 means draw
    // return <is_done, winner_index>
    virtual std::pair<bool, int> get_done_winner() const = 0;

    // get current player index
    virtual int get_current_player() const = 0;

    // return a deep copy of the game
    virtual std::shared_ptr<Derived> clone() const = 0;

    // Select an action for a rollout. Games can override this to add a
    // domain-specific policy while keeping uniform random play as default.
    virtual int select_rollout_action(
        const ActionList& actions,
        std::mt19937& rand_engine,
        int rollout_policy) const {
        (void)rollout_policy;
        std::uniform_int_distribution<int> distribution(0, actions.size() - 1);
        return actions[distribution(rand_engine)];
    }

    // Return a value from the requested player's perspective for a
    // non-terminal rollout cutoff. The default keeps generic games neutral.
    virtual float evaluate(int player) const {
        (void)player;
        return 0.0f;
    }

    // // function that returns the value of the game and the policy
    // // Output: vector<vaild_action_index, action_probs, value_of_current_game>
    // virtual std::tuple<std::vector<int>, std::vector<float>, float> policy_value_fn() const = 0;
};
