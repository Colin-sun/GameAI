// define standard interfaces
#pragma once
#include <vector>
#include <functional>
#include <unordered_map>
#include <memory>

// Game class
// All game should inherit from this class and implement all the functions
template<typename Derived>
class GameBase {
public:
    // get legal actions
    // action_index should be positive
    virtual const std::vector<int> get_valid_actions() const = 0;

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

    // // function that returns the value of the game and the policy
    // // Output: vector<vaild_action_index, action_probs, value_of_current_game>
    // virtual std::tuple<std::vector<int>, std::vector<float>, float> policy_value_fn() const = 0;
};