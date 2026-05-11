// TreeNode.h
// define TreeNode class
#ifndef TREENODE_H
#define TREENODE_H

#include <vector>
#include <memory>
#include <unordered_map>
#include <cmath>
#include <limits>
#include <stdexcept>

template <typename ActionList>
class TreeNode : public std::enable_shared_from_this<TreeNode<ActionList>> {
public:
    // Parent is weak to avoid reference cycles; children own the subtree.
    std::weak_ptr<TreeNode<ActionList>> _parent;
    std::unordered_map<int, std::shared_ptr<TreeNode<ActionList>>> _children; // <action_index, child>

    int _n_visits;
    float _Q;
    float _u;
    float prior_p;

    // Constructor
    TreeNode(std::shared_ptr<TreeNode<ActionList>> parent, float prior_p)
        : _parent(parent), _n_visits(0), _Q(0.0), _u(0.0), prior_p(prior_p) {
    }
    // Default destructor

    // Expand tree by creating new children.
    // actions: a list of action_indexs
    // action_priors: a list of actions' prior probability
    // according to the policy function.
    void expand(const ActionList actions, const std::vector<float>& action_priors) {
        for (size_t i = 0; i < actions.size(); i++) {
            std::shared_ptr<TreeNode<ActionList>> child =
                std::make_shared<TreeNode<ActionList>>(this->shared_from_this(), action_priors[i]);
            _children[actions[i]] = child;
        }
    }

    // for mcts_pure
    void expand(const ActionList actions) {
        for (int i = 0; i < actions.size(); i++) {
            // use shared_from_this() for memory management
            _children[actions[i]] = std::make_shared<TreeNode<ActionList>>(this->shared_from_this(), 1.0);
        }
    }

    // Select action among children that gives maximum action value Q
    // plus bonus u(P).
    // Input: c_puct from MCTS class
    // Return: A tuple of(action, next_node)
    std::pair<int, std::shared_ptr<TreeNode<ActionList>>> select(float c_puct) {
        if (_children.empty()) {
            throw std::runtime_error("Cannot select from leaf node");
        }

        int best_action = 0;
        std::shared_ptr<TreeNode<ActionList>> best_node{ nullptr };
        float best_value = -std::numeric_limits<float>::max();

        for (auto& pair : _children) {
            int action = pair.first;
            std::shared_ptr<TreeNode> node = pair.second;
            float value = node->get_value(c_puct);

            if (value > best_value) {
                best_value = value;
                best_action = action;
                best_node = node;
            }
        }
        return std::make_pair(best_action, best_node);
    }

    // Update node values from leaf evaluation.
    // Input: leaf_value: the value of subtree evaluation from the current player's perspective.
    void update(float leaf_value) {
        _n_visits++;
        _Q += (leaf_value - _Q) / _n_visits;
    }

    // Like a call to update(), but applied recursively for all ancestors.
    void update_recursive(float leaf_value) {
        if (auto parent = _parent.lock()) {
            parent->update_recursive(-leaf_value);
        }
        update(leaf_value);
    }

    // Calculate and return the value for this node.
    // It is a combination of leaf evaluations Q, and this node's prior
    // adjusted for its visit count, u.
    // c_puct: a number in(0, inf) controlling the relative impact of
    // value Q, and prior probability P, on this node's score.
    float get_value(float c_puct) {
        auto parent = _parent.lock();
        float parent_visits = parent ? static_cast<float>(parent->_n_visits) : 0.0f;
        _u = (c_puct * prior_p * std::sqrt(parent_visits)) / (1 + _n_visits);
        return _Q + _u;
    }

    // Check if this node is a leaf node(no children).
    bool is_leaf() {
        return _children.empty();
    }

    // Check if this is the root node.
    bool is_root() {
        return _parent.expired();
    }
};

#endif // TREENODE_H
