// TreeNode.h
// define TreeNode class
#ifndef TREENODE_H
#define TREENODE_H

#include <vector>
#include <memory>
#include <unordered_map>

class TreeNode : public std::enable_shared_from_this<TreeNode> {
public:
    // Parent and child nodes are managed using shared_ptr
    std::shared_ptr<TreeNode> _parent;
    std::unordered_map<int, std::shared_ptr<TreeNode>> _children; // <action_index, child>

    int _n_visits;
    float _Q;
    float _u;
    float prior_p;

    // Constructor
    TreeNode(std::shared_ptr<TreeNode> parent, float prior_p);
    // Default destructor

    // Expand tree by creating new children.
    // actions: a list of action_indexs
    // action_priors: a list of actions' prior probability
    // according to the policy function.
    void expand(const std::vector<int>& actions, const std::vector<float>& action_priors);

    // for mcts_pure
    void expand(const std::vector<int>& actions);

    // Select action among children that gives maximum action value Q
    // plus bonus u(P).
    // Input: c_puct from MCTS class
    // Return: A tuple of(action, next_node)
    std::pair<int, std::shared_ptr<TreeNode>> select(float c_puct);

    // Update node values from leaf evaluation.
    // Input: leaf_value: the value of subtree evaluation from the current player's perspective.
    void update(float leaf_value);

    // Like a call to update(), but applied recursively for all ancestors.
    void update_recursive(float leaf_value);

    // Calculate and return the value for this node.
    // It is a combination of leaf evaluations Q, and this node's prior
    // adjusted for its visit count, u.
    // c_puct: a number in(0, inf) controlling the relative impact of
    // value Q, and prior probability P, on this node's score.
    float get_value(float c_puct);

    // Check if this node is a leaf node(no children).
    bool is_leaf();

    // Check if this is the root node.
    bool is_root();
};

#endif // TREENODE_H