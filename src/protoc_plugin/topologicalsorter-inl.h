// Copyright 2010 Google Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//      http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ------------------------------------------------------------------------

// Author: plakal@google.com (Manoj Plakal)
//
// Implemention of TopologicalSorter.

#ifndef _TOPOLOGICALSORTER_INL_H__
#define _TOPOLOGICALSORTER_INL_H__

#include <map>
#include <queue>
#include <vector>

#include "protoc_plugin/topologicalsorter.h"


// Test to see if a set, map, hash_set or hash_map contains a particular key.
// Returns true if the key is in the collection.
template <class Collection, class Key>
bool ContainsKey(const Collection& collection, const Key& key) {
  typename Collection::const_iterator it = collection.find(key);
  return it != collection.end();
}

template <typename T>
TopologicalSorter<T>::TopologicalSorter()
    : started_traversal_(false),
      num_nodes_visited_(0) {
}

template <typename T>
TopologicalSorter<T>::~TopologicalSorter() {
}

template <typename T>
void TopologicalSorter<T>::AddNode(const T& node) {
  GOOGLE_CHECK(!started_traversal_) << "Cannot add nodes after starting traversal";

  // Initialize the node if it has not already been added.
  if (!ContainsKey(edges_, node)) {
    // This node has no outgoing edges so far.
    edges_[node].clear();

    // And no incoming edges so far.
    indegrees_[node] = 0;
  }
}

template <typename T>
void TopologicalSorter<T>::AddEdge(const T& from, const T& to) {
  GOOGLE_CHECK(!started_traversal_) << "Cannot add edges after starting traversal";

  // Add the endpoints.
  AddNode(from);
  AddNode(to);

  // Add the edge if it does not yet exist.
  vector<T>* from_edges = &edges_[from];
  if (find(from_edges->begin(), from_edges->end(), to) == from_edges->end()) {
    from_edges->push_back(to);

    // Increment the indegree of the destination node.
    ++indegrees_[to];
  }
}

template <typename T>
bool TopologicalSorter<T>::GetNext(T* node, bool* cyclic) {
  InitTraversal();

  *cyclic = false;  // Innocent until proven guilty.

  // OK, visited all nodes.
  if (next_nodes_.size() == 0) {
    // The queue is empty, just check that we have indeed
    // exhausted all nodes.
    if (num_nodes_visited_ != edges_.size()) {
      GOOGLE_LOG(WARNING) << "Not all nodes have been visited, but there aren't any "
              << "zero-indegree nodes available.  This graph is cyclic!";
      *cyclic = true;
    }
    return false;
  }

  // Pop the next node in order.
  *node = next_nodes_.front();
  next_nodes_.pop();
  ++num_nodes_visited_;

  // Decrement the indegree of all nodes reached from this node
  // and add them to the node queue if they are now sources.
  fringe_.clear();
  const vector<T>& out_nodes = edges_[*node];
  for (int i = 0; i < out_nodes.size(); ++i) {
    const T& out_node = out_nodes[i];
    --indegrees_[out_node];
    if (indegrees_[out_node] == 0) {
      next_nodes_.push(out_node);
      fringe_.push_back(out_node);
    }
  }

  return true;
}

template <typename T>
const vector<T> &TopologicalSorter<T>::GetCurrentFringe() {
  InitTraversal();
  return fringe_;
}

template <typename T>
void TopologicalSorter<T>::InitTraversal() {
  if (!started_traversal_) {
    // Initialize node queue with all sources (zero-indegree nodes).
    for (typename map<T, int>::iterator it = indegrees_.begin();
         it != indegrees_.end();
         ++it) {
      //LOG(INFO) << "Indegree(" << it->first << ")=" << it->second;
      if (it->second == 0) {
        next_nodes_.push(it->first);
        fringe_.push_back(it->first);
      }
    }

    started_traversal_ = true;
  }
}

#endif  // _TOPOLOGICALSORTER_INL_H__
