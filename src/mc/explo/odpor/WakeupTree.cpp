/* Copyright (c) 2008-2024. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include "src/mc/explo/odpor/WakeupTree.hpp"
#include "src/mc/explo/odpor/Execution.hpp"
#include "src/mc/explo/odpor/odpor_forward.hpp"
#include "xbt/asserts.h"
#include "xbt/string.hpp"

#include <algorithm>
#include <memory>

XBT_LOG_NEW_DEFAULT_SUBCATEGORY(mc_wut, mc, "Logging specific to ODPOR WakeupTrees");

namespace simgrid::mc::odpor {

void WakeupTreeNode::add_child(WakeupTreeNode* node)
{
  this->children_.push_back(node);
  node->parent_ = this;
  node->sequence_ = this->sequence_;
  node->sequence_.emplace_back(node->action_);
}

std::string WakeupTreeNode::string_of_whole_tree(const std::string& prefix, bool is_first, bool is_last) const
{

  std::string final_string = action_ == nullptr
                                 ? "<>\n"
                                 : prefix + (is_last ? "└──" : "├──") + "Actor " + std::to_string(action_->aid_) +
                                       ": " + action_->to_string(true) + "\n";
  bool is_next_first       = true;
  for (auto node_it = children_.begin(); node_it != children_.end(); node_it++) {
    auto node                    = *node_it;
    const std::string new_prefix = prefix + (is_last ? "    " : "│   ");
    final_string += node->string_of_whole_tree(new_prefix, is_next_first, std::next(node_it) == children_.end());
    is_next_first = false;
  }
  return final_string;
}

const PartialExecution& WakeupTreeNode::get_sequence() const
{
  return sequence_;
}

void WakeupTreeNode::detatch_from_parent()
{
  if (parent_ != nullptr) {
    // TODO: There may be a better method
    // of keeping track of a node's reference to
    // its parent, perhaps keeping track
    // of a std::list<>::iterator instead.
    // This would allow us to detach a node
    // in O(1) instead of O(|children|) time
    parent_->children_.remove(this);
  }
}

WakeupTree::WakeupTree() : WakeupTree(std::make_unique<WakeupTreeNode>()) {}
WakeupTree::WakeupTree(std::unique_ptr<WakeupTreeNode> root) : root_(root.get())
{
  this->insert_node(std::move(root));
}

std::vector<std::string> WakeupTree::get_single_process_texts() const
{
  std::vector<std::string> trace;
  for (const auto* child : root_->children_) {
    const auto t = child->get_action();
    auto message = xbt::string_printf("Actor %ld: %s", t->aid_, t->to_string(true).c_str());
    trace.emplace_back(std::move(message));
  }
  return trace;
}

std::optional<aid_t> WakeupTree::get_min_single_process_actor() const
{
  if (const auto node = get_min_single_process_node(); node.has_value()) {
    return node.value()->get_actor();
  }
  return std::nullopt;
}

std::optional<WakeupTreeNode*> WakeupTree::get_min_single_process_node() const
{
  if (empty()) {
    return std::nullopt;
  }
  // INVARIANT: The induced post-order relation always places children
  // in order before the parent. The list of children maintained by
  // each node represents that ordering, and the first child of
  // the root is by definition the smallest of the single-process nodes
  xbt_assert(not this->root_->children_.empty(), "What the");
  return this->root_->children_.front();
}

std::string WakeupTree::string_of_whole_tree() const
{
  return "\n" + root_->string_of_whole_tree("", false, true);
}

WakeupTree WakeupTree::make_subtree_rooted_at(WakeupTreeNode* root)
{
  xbt_assert(root != nullptr, "Making a subtree of a nullptr won't do anything. Fix Me.");
  // Perform a BFS search to perform a deep copy of the portion
  // of the tree underneath and including `root`. Note that `root`
  // is contained within the context of a *different* wakeup tree;
  // hence, we have to be careful to update each node's children
  // appropriately
  auto subtree = WakeupTree();

  std::list<std::pair<WakeupTreeNode*, WakeupTreeNode*>> frontier{std::make_pair(root, subtree.root_)};
  while (not frontier.empty()) {
    auto [node_in_other_tree, subtree_equivalent] = frontier.front();
    frontier.pop_front();

    // For each child of the node corresponding to that in `subtree`,
    // make clones of each of its children and add them to `frontier`
    // to that their children are added, and so on.
    for (WakeupTreeNode* child_in_other_tree : node_in_other_tree->get_ordered_children()) {
      WakeupTreeNode* child_equivalent = subtree.make_node(child_in_other_tree->get_action());
      subtree_equivalent->add_child(child_equivalent);
      frontier.push_back(std::make_pair(child_in_other_tree, child_equivalent));
    }
  }
  return subtree;
}

void WakeupTree::remove_subtree_rooted_at(WakeupTreeNode* root)
{
  if (not contains(root)) {
    throw std::invalid_argument("Attempting to remove a subtree pivoted from a node "
                                "that is not contained in this wakeup tree");
  }

  std::list<WakeupTreeNode*> subtree_contents{root};
  std::list<WakeupTreeNode*> frontier{root};
  while (not frontier.empty()) {
    const auto* node = frontier.front();
    frontier.pop_front();
    for (const auto& child : node->get_ordered_children()) {
      frontier.push_back(child);
      subtree_contents.push_back(child);
    }
  }

  // After having found each node with BFS, now we can
  // remove them. This prevents the "joys" of iteration during mutation.
  // We also remove the `root` from being referenced by its own parent (since
  // it will soon be destroyed)
  root->detatch_from_parent();
  for (WakeupTreeNode* node_to_remove : subtree_contents) {
    this->remove_node(node_to_remove);
  }
}

void WakeupTree::remove_subtree_at_aid(aid_t proc)
{
  for (const auto& child : root_->get_ordered_children())
    if (child->get_actor() == proc) {
      this->remove_subtree_rooted_at(child);
      break;
    }
}

void WakeupTree::remove_min_single_process_subtree()
{
  if (const auto node = get_min_single_process_node(); node.has_value()) {
    remove_subtree_rooted_at(node.value());
  }
}

bool WakeupTree::contains(const WakeupTreeNode* node) const
{
  return std::find_if(this->nodes_.begin(), this->nodes_.end(), [=](const auto& pair) { return pair.first == node; }) !=
         this->nodes_.end();
}

WakeupTreeNode* WakeupTree::make_node(std::shared_ptr<Transition> u)
{
  auto node                 = std::make_unique<WakeupTreeNode>(std::move(u));
  auto* node_handle         = node.get();
  this->nodes_[node_handle] = std::move(node);
  return node_handle;
}

void WakeupTree::insert_node(std::unique_ptr<WakeupTreeNode> node)
{
  auto* node_handle         = node.get();
  this->nodes_[node_handle] = std::move(node);
}

void WakeupTree::remove_node(WakeupTreeNode* node)
{
  this->nodes_.erase(node);
}

void WakeupTree::insert_at_root(std::shared_ptr<Transition> u)
{
  WakeupTreeNode* new_node = this->make_node(u);
  this->root_->add_child(new_node);
}

InsertionResult WakeupTreeNode::recursive_insert(WakeupTree& father, PartialExecution& w)
{
  // If we reached a leaf, then there is nothing to insert. The exploration from this leaf
  // will eventually take care of this execution if needed
  if (this->is_leaf() and not this->is_root())
    return InsertionResult::leaf;

  for (auto& node : this->children_) {
    const auto& next_E_p = node->action_;
    // Is `p in `I_[E](w)`?
    if (const aid_t p = next_E_p->aid_; Execution::is_initial_after_execution_of(w, p)) {
      // Remove `p` from w and continue with this node

      // INVARIANT: If `p` occurs in `w`, it had better refer to the same
      // transition referenced by `v`. Unfortunately, we have two
      // sources of truth here which can be manipulated at the same
      // time as arguments to the function. If ODPOR works correctly,
      // they should always refer to the same value; but as a sanity check,
      // we have an assert that tests that at least the types are the same.
      const auto action_by_p_in_w =
          std::find_if(w.begin(), w.end(), [=](const auto& action) { return action->aid_ == p; });
      xbt_assert(action_by_p_in_w != w.end(), "Invariant violated: actor `p` "
                                              "is claimed to be an initial after `w` but is "
                                              "not actually contained in `w`. This indicates that there "
                                              "is a bug computing initials");
      const auto& w_action = *action_by_p_in_w;
      xbt_assert(w_action->type_ == next_E_p->type_,
                 "Invariant violated: `v` claims that actor `%ld` executes '%s' while "
                 "`w` claims that it executes '%s'. These two partial executions both "
                 "refer to `next_[E](p)`, which should be the same",
                 p, next_E_p->to_string(false).c_str(), w_action->to_string(false).c_str());
      w.erase(action_by_p_in_w);
      return node->recursive_insert(father, w);
    }
    // Is `E ⊢ p ◇ w`?
    else if (Execution::is_independent_with_execution_of(w, next_E_p)) {
      // Nothing to remove, we simply move on

      // INVARIANT: Note that it is impossible for `p` to be
      // excluded from the set `I_[E](w)` BUT ALSO be contained in
      // `w` itself if `E ⊢ p ◇ w` (intuitively, the fact that `E ⊢ p ◇ w`
      // means that are able to move `p` anywhere in `w` IF it occurred, so
      // if it really does occur we know it must then be an initial).
      // We assert this is the case here
      const auto action_by_p_in_w =
          std::find_if(w.begin(), w.end(), [=](const auto& action) { return action->aid_ == p; });
      xbt_assert(action_by_p_in_w == w.end(),
                 "Invariant violated: We claimed that actor `%ld` is not an initial "
                 "after `w`, yet it's independent with all actions of `w` AND occurs in `w`."
                 "This indicates that there is a bug computing initials",
                 p);
      return node->recursive_insert(father, w);
    }
  }
  // We can't insert w anymore from there. Let's insert the remaining and tell the world about that
  father.insert_sequence_after(this, w);
  return this->is_root() ? InsertionResult::root : InsertionResult::interior_node;
}

InsertionResult WakeupTree::insert(const PartialExecution& w)
{
  auto w_copy = w;
  return this->root_->recursive_insert(*this, w_copy);
}

WakeupTreeNode* WakeupTreeNode::recursive_insert_and_get_inserted_seq(WakeupTree& father, PartialExecution& w)
{
  // If we reached a leaf, then there is nothing to insert. The exploration from this leaf
  // will eventually take care of this execution if needed
  if (this->is_leaf())
    return this;

  for (auto& node : this->children_) {
    const auto& next_E_p = node->action_;
    // Is `p in `I_[E](w)`?
    if (const aid_t p = next_E_p->aid_; Execution::is_initial_after_execution_of(w, p)) {
      // Remove `p` from w and continue with this node

      // INVARIANT: If `p` occurs in `w`, it had better refer to the same
      // transition referenced by `v`. Unfortunately, we have two
      // sources of truth here which can be manipulated at the same
      // time as arguments to the function. If ODPOR works correctly,
      // they should always refer to the same value; but as a sanity check,
      // we have an assert that tests that at least the types are the same.
      const auto action_by_p_in_w =
          std::find_if(w.begin(), w.end(), [=](const auto& action) { return action->aid_ == p; });
      xbt_assert(action_by_p_in_w != w.end(), "Invariant violated: actor `p` "
                                              "is claimed to be an initial after `w` but is "
                                              "not actually contained in `w`. This indicates that there "
                                              "is a bug computing initials");
      const auto& w_action = *action_by_p_in_w;
      xbt_assert(w_action->type_ == next_E_p->type_,
                 "Invariant violated: `v` claims that actor `%ld` executes '%s' while "
                 "`w` claims that it executes '%s'. These two partial executions both "
                 "refer to `next_[E](p)`, which should be the same",
                 p, next_E_p->to_string(false).c_str(), w_action->to_string(false).c_str());
      w.erase(action_by_p_in_w);
      return node->recursive_insert_and_get_inserted_seq(father, w);
    }
    // Is `E ⊢ p ◇ w`?
    else if (Execution::is_independent_with_execution_of(w, next_E_p)) {
      // Nothing to remove, we simply move on

      // INVARIANT: Note that it is impossible for `p` to be
      // excluded from the set `I_[E](w)` BUT ALSO be contained in
      // `w` itself if `E ⊢ p ◇ w` (intuitively, the fact that `E ⊢ p ◇ w`
      // means that are able to move `p` anywhere in `w` IF it occurred, so
      // if it really does occur we know it must then be an initial).
      // We assert this is the case here
      const auto action_by_p_in_w =
          std::find_if(w.begin(), w.end(), [=](const auto& action) { return action->aid_ == p; });
      xbt_assert(action_by_p_in_w == w.end(),
                 "Invariant violated: We claimed that actor `%ld` is not an initial "
                 "after `w`, yet it's independent with all actions of `w` AND occurs in `w`."
                 "This indicates that there is a bug computing initials",
                 p);
      return node->recursive_insert_and_get_inserted_seq(father, w);
    }
  }
  // We can't insert w anymore from there. Let's insert the remaining and tell the world about that
  return father.insert_sequence_after(this, w);
}

const PartialExecution WakeupTree::insert_and_get_inserted_seq(const PartialExecution& w)
{
  auto w_copy                   = w;
  WakeupTreeNode* inserted_node = this->root_->recursive_insert_and_get_inserted_seq(*this, w_copy);
  return inserted_node->get_sequence();
}

WakeupTreeNode* WakeupTree::insert_sequence_after(WakeupTreeNode* node, const PartialExecution& w)
{
  WakeupTreeNode* cur_node = node;
  for (const auto& w_i : w) {
    WakeupTreeNode* new_node = this->make_node(w_i);
    cur_node->add_child(new_node);
    cur_node = new_node;
  }
  return cur_node;
}

WakeupTreeNode* WakeupTree::get_node_after_actor(aid_t aid) const
{
  for (auto const node : root_->children_)
    if (node->get_actor() == aid)
      return node;

  return nullptr;
}

bool WakeupTreeNode::have_same_content(const WakeupTreeNode& n2) const
{
  return this->get_action() == n2.get_action();
}

bool WakeupTreeNode::is_contained_in(WakeupTreeNode& other_tree) const
{

  if (not have_same_content(other_tree))
    return false;

  for (auto child : children_) {
    auto other_candidate = std::find_if(other_tree.children_.begin(), other_tree.children_.end(),
                                        [=](const auto& node) { return child->have_same_content(*node); });
    if (other_candidate == other_tree.children_.end())
      return false;
    if (not child->is_contained_in(**other_candidate))
      return false;
  }

  return true;
}

bool WakeupTree::is_contained_in(WakeupTree& other_tree) const
{
  if (root_ == nullptr)
    return true;
  if (other_tree.root_ == nullptr)
    return false; // if we are not empty but other is, we are not contained
  return this->root_->is_contained_in(*other_tree.root_);
}

void WakeupTree::force_insert(const PartialExecution& seq)
{
  WakeupTreeNode* cur_node = root_;
  for (const auto& w_i : seq) {
    if (auto node_after_aid = cur_node->get_node_after_actor(w_i->aid_); node_after_aid != nullptr) {
      cur_node = node_after_aid;
      continue;
    }

    WakeupTreeNode* new_node = this->make_node(w_i);
    cur_node->add_child(new_node);
    cur_node = new_node;
  }
}

} // namespace simgrid::mc::odpor
