// TreeNode.cpp
#include "TreeNode.h"
#include <limits>
#include <stdexcept>
#include <cmath>

TreeNode::TreeNode(std::shared_ptr<TreeNode> parent, float prior_p)
    : _parent(parent), _n_visits(0), _Q(0.0), _u(0.0), prior_p(prior_p) {
}

void TreeNode::expand(const std::vector<int>& actions, const std::vector<float>& action_priors) {
    for (size_t i = 0; i < actions.size(); i++) {
        std::shared_ptr<TreeNode> child = std::make_shared<TreeNode>(shared_from_this(), action_priors[i]);
        _children[actions[i]] = child;
    }
}

void TreeNode::expand(const std::vector<int>& actions) {
    for (int action : actions) {
        // use shared_from_this() for memory management
        _children[action] = std::make_shared<TreeNode>(shared_from_this(), 1.0);
    }
}

std::pair<int, std::shared_ptr<TreeNode>> TreeNode::select(float c_puct) {
    if (_children.empty()) {
        throw std::runtime_error("Cannot select from leaf node");
    }

    int best_action = 0;
    std::shared_ptr<TreeNode> best_node{ nullptr };
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

void TreeNode::update(float leaf_value) {
    _n_visits++;
    _Q += (leaf_value - _Q) / _n_visits;
}

void TreeNode::update_recursive(float leaf_value) {
    if (_parent) {
        _parent->update_recursive(-leaf_value);
    }
    update(leaf_value);
}

float TreeNode::get_value(float c_puct) {
    _u = (c_puct * prior_p * std::sqrt(_parent->_n_visits)) / (1 + _n_visits);
    return _Q + _u;
}

bool TreeNode::is_leaf() {
    return _children.empty();
}

bool TreeNode::is_root() {
    return _parent == nullptr;
}