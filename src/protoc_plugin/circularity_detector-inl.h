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

#ifndef _CIRCULARITY_DETECTOR_INL_H__
#define _CIRCULARITY_DETECTOR_INL_H__


#include <algorithm>
#include <map>
#include <set>
#include <stack>
#include <vector>

#include "protoc_plugin/linked_ptr.h"
#include "protoc_plugin/circularity_detector.h"

template <typename Node, typename CompareNode>
class CircularityDetector<Node, CompareNode>::Impl {
 public:
  Impl()
      : need_traversal_(false),
        untraversed_node_counter_(0),
        has_cycle_(false) {}

  void AddEdge(const Node &from, const Node &to) {
    // Add an edge to the graph, creating the nodes if necessary.
    if (from == to) {
      // Self-loops do not affect the computation of the strongly-connected
      // components, but they do count as cycles.
      has_cycle_ = true;
    } else {
      Get(from)->edges.insert(Get(to));
      need_traversal_ = true;
    }
  }

  bool IsCircular(const Node &left, const Node &right) {
    TraverseIfNecessary();
    return Get(left)->component_id == Get(right)->component_id;
  }

  bool HasCycle() {
    TraverseIfNecessary();
    return has_cycle_;
  }

  int GetComponentId(const Node& node) {
    TraverseIfNecessary();
    return Get(node)->component_id;
  }

 private:
  // A MapValue represents a node in a directed graph.
  // Each node has two integer attributes.
  struct MapValue {
    // The set of nodes directly reachable
    // from this node through its outgoing edges.
    set<MapValue*> edges;

    // The traversal algorithm assigns the same value to
    // component_id for each member of a strongly-connected component.
    int component_id;

    // Temporary attribute used by the traversal algorithm.
    int depth_first_number;

    explicit MapValue(int id) : component_id(id) {}
  };

  // A map from user-visible labels to the actual nodes.
  typedef linked_ptr<MapValue> ValuePtr;
  typedef map<Node, ValuePtr, CompareNode> NodeMap;

  // DualStack contains two stacks, a control_stack and a data_stack.
  // Both stacks are pushed in unision, but they are popped separately.
  // DualStack is a helper class, used only by FindComponents, to implement
  // a recursive algorithm iteratively using an STL stack, to avoid the
  // problem of running out of program stack space because of deep recursion
  // while processing large graphs.
  // 
  // The intent is not to provide an abstraction to FindComponents, but merely
  // to package up the data manipulated by the algorithm.  Ideally, DualStack
  // would be a local class nested in FindComponents, but then it wouldn't
  // compile because of restrictions on instantiating templates with types
  // defined in local classes.
  class DualStack {
   public:
    struct StackFrame {
      StackFrame(int df, MapValue *n)
          : min_depth_first_number(df),
            node(n),
            here(n->edges.begin()),
            end(n->edges.end()) {}
      int min_depth_first_number;
      MapValue *node;
      typename set<MapValue*>::iterator here, end;
    };

    DualStack()
       : depth_first_counter_(0),
         component_counter_(0),
         has_nontrivial_scc_(false) {}

    // Push a node onto both stacks.
    void Push(MapValue *node) {
      value_stack_.push(node);
      node->depth_first_number = ++depth_first_counter_;
      control_stack_.push_back(StackFrame(depth_first_counter_, node));
    }

    bool ControlEmpty() {return control_stack_.empty();}

    // Return a pointer to the top of the control stack.
    // Top may be called only if !ControlEmpty()
    StackFrame *Top() {return &control_stack_.back();}

    // Pop the control stack.
    void Pop() {control_stack_.pop_back();}

    // Pop a strongly-connected component off the data stack.
    void PopComponent() {
      // Create a new id for all the nodes in the component.
      component_counter_ += 1;

      const MapValue *const top_node = Top()->node;

      // Check for a component whose size is larger than one.
      if (value_stack_.top() != top_node) {
        has_nontrivial_scc_ = true;
      }

      // The strongly connected component consists of all the nodes
      // on the top of the value stack, up to and including the node
      // that is on top of the control stack.  Pop them all off the
      // data stack, while assigning them all the same component_id.
      MapValue *popped;
      do {
        popped = value_stack_.top();
        value_stack_.pop();
        popped->component_id = component_counter_;
      } while (popped != top_node);
    }

    bool HasNontrivialSCC() {return has_nontrivial_scc_;}
   private:
    // Used to implement a recursive algorithm iteratively.
    vector<StackFrame> control_stack_;

    // Output buffer used to hold partially-discovered components.
    stack<MapValue*> value_stack_;

    // Counter used to generate depth-first numbers.
    int depth_first_counter_;

    // Counter used to generate component ids.
    int component_counter_;

    // Set to true when a nontrivial component is dicovered.
    bool has_nontrivial_scc_;

    GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(DualStack);
  };

  // Finds or creates the map node for a label.
  MapValue* Get(const Node &key);

  // Iterates the subtree with root graph_node of a depth-first spanning
  // tree.  For each node N in the subtree, either marks the strongly-
  // connected component containing N, or leaves N in value_stack_.
  // Returns a boolean indicating whether it located any nontrivial
  // components.
  void FindComponents(MapValue *root, DualStack *node_stack);

  // Iterates the depth-first spanning forest of the graph,
  // calling FindComponents on the root of each depth-first spanning tree.
  // Sets the component_id and depth_first_number of every node, and
  // returns an indication of whether the graph contains any nontrivial
  // components  Does not add or delete nodes, or change the mapping from
  // Node to ValuePtr.
  bool Traverse(NodeMap *graph);

  // Traverse the graph if the an edge has been added since
  // the last time the graph was traversed.
  void TraverseIfNecessary();

  // The input to the algorithm.
  NodeMap graph_;

  // Indicates whether an edge has been created since the last traversal.
  bool need_traversal_;

  // The number of nodes that have been created since the last traversal.
  int untraversed_node_counter_;

  // Set when a self-loop or nontrivial SCC is discovered.  Since we do not
  // export a method to delete an edge, once this member becomes true, it
  // will always remain so.
  bool has_cycle_;

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(Impl);
};

template <typename Node, typename CompareNode>
typename CircularityDetector<Node, CompareNode>::Impl::MapValue*
CircularityDetector<Node, CompareNode>::Impl::Get(const Node &key) {
  ValuePtr &value = graph_[key];
  if (value == NULL) {
    // If nodes (but not edges) are created between traversals,
    // assign them a component id guaranteed not to match anything
    // else.  Once an edge has been created, it doesn't matter what
    // we do here, since the edge will force a traversal, which will
    // assign a new component id.
    value = ValuePtr(new MapValue(-(++untraversed_node_counter_)));
  }
  return value.get();
}

// Here is the classical Strongly-Connected Components algorithm,
// derived from the one in Section 5.5 of _The Design and Analysis of Computer
// Algorithms_ (1976), by Aho, Hopcroft, and Ullman.  The algorithm is short,
// but also quite subtle.  For an explanation, dig up the book, or read the
// description I wrote at http://wiki/Main/StronglyConnectedComponents.
//
// int FindComponents(MapValue* graph_node) {
//   if (graph_node->depth_first_number != 0) {
//     return graph_node->depth_first_number;
//   }
//   value_stack_.push(graph_node);
// 
//   int result = graph_node->depth_first_number = ++depth_first_counter_;
// 
//   for (set<MapValue*>::iterator
//        e = graph_node->edges.begin(); e != graph_node->edges.end(); e++) {
//     if ((*e)->component_id == 0) {
//       result = min(result, FindComponents(*e));
//     }
//   }
//   if (result == graph_node->depth_first_number) {
//     component_counter_ += 1;
//     for (;;) {
//       MapValue *popped = value_stack_.top();
//       value_stack_.pop();
//       popped->component_id = component_counter_;
//       if (popped == graph_node) break;
//     }
//   }
//   return result;
// }
//
// A problem with the algorithm as presented, however, is that the level of
// recursion can be as large as the longest path in the graph.  Since C++
// implementations have a fixed-size recursion stack, there is a capacity
// limit on the size of the graph that can be processed.
//
// The solution to this problem is to implement the algorithm iteratively
// using a stack.  A recursive program can be translated to an interative
// one by using a stack of virtual machine implementations of the body of
// the recursive routine.  A recursive call is implemented by pushing a new
// virtual machine onto the stack, and recursive return is implemented with
// a pop.  The main body of of the interative solution is an intepreter that
// executes the state on top of the stack.
//
// Specifically, the virtual machines FindComponents push onto the stack
// iterate the edges emanating from a single node N, keeping track of the
// minimum depth first number reached.  If that value equals the depth
// first number of N, it pops off a strongly-connected component.

template <typename Node, typename CompareNode>
void CircularityDetector<Node, CompareNode>::Impl::FindComponents(
     CircularityDetector<Node, CompareNode>::Impl::MapValue* root,
     CircularityDetector<Node, CompareNode>::Impl::DualStack *node_stack) {
  node_stack->Push(root);

  for(;;) {
    if (node_stack->Top()->here == node_stack->Top()->end) {
      // The iteration is complete.  min_depth_first_number is smallest
      // depth-first number reachable from node_stack->Top()->node.
      const int min_depth_first_number =
          node_stack->Top()->min_depth_first_number;
      if (min_depth_first_number ==
          node_stack->Top()->node->depth_first_number) {
        // We have located a strongly-connected component.
        node_stack->PopComponent();
      }
      // Return from the recursion.
      node_stack->Pop();
      
      // If the stack is empty, the algorithm is complete.
      if (node_stack->ControlEmpty()) return;

      // Otherwise propagate the value returned by the recursive call
      // up to the next level.
      node_stack->Top()->min_depth_first_number =
          min(node_stack->Top()->min_depth_first_number,
              min_depth_first_number);
    } else {
      // Advance the iterator by one.
      MapValue *node = *(node_stack->Top()->here)++;
      if (node->component_id == 0) {
        if (node->depth_first_number == 0) {
          // Recursively explore the node to which the edge leads.
          node_stack->Push(node);
        } else {
          // Use the value previously stored there.
          node_stack->Top()->min_depth_first_number =
              min(node_stack->Top()->min_depth_first_number,
                  node->depth_first_number);
        }
      }
    }
  }
}

template <typename Node, typename CompareNode>
bool CircularityDetector<Node, CompareNode>::Impl::Traverse(
     NodeMap* graph) {
  DualStack node_stack;
  typedef typename NodeMap::const_iterator MapConstIter;

  // Initialize the state of the graph.
  for (MapConstIter m = graph->begin(); m != graph->end(); ++m) {
    m->second->component_id = 0;
    m->second->depth_first_number = 0;
  }

  // Find the strongly connected components
  // of the graph using depth-first search.
  for (MapConstIter m = graph->begin(); m != graph->end(); ++m) {
    if (m->second->component_id == 0) {
      FindComponents(m->second.get(), &node_stack);
    }
  }
  return node_stack.HasNontrivialSCC();
}

template <typename Node, typename CompareNode>
void CircularityDetector<Node, CompareNode>::
     Impl::TraverseIfNecessary() {
  if (need_traversal_) {
    // We must use the boolean or operator here to avoid losing track
    // of any self-loops, which are not entered into the graph.  This
    // trick only works because we never delete an edge from the graph.
    has_cycle_ |= Traverse(&graph_);
    need_traversal_ = false;
    untraversed_node_counter_ = 0;
  }
}

// CircularityDetector is simply a skin over CircularityDetector::Impl.
template <typename Node, typename CompareNode>
CircularityDetector<Node, CompareNode>::CircularityDetector()
    : impl_(new Impl) {}

template <typename Node, typename CompareNode>
CircularityDetector<Node, CompareNode>::~CircularityDetector() {
  delete impl_;
}

template <typename Node, typename CompareNode>
void CircularityDetector<Node, CompareNode>::AddEdge(
     const Node &from, const Node &to) {
  impl_->AddEdge(from, to);
}

template <typename Node, typename CompareNode>
bool CircularityDetector<Node, CompareNode>::IsCircular(
     const Node &left, const Node &right) {
  return impl_->IsCircular(left, right);
}

template <typename Node, typename CompareNode>
bool CircularityDetector<Node, CompareNode>::HasCycle() {
  return impl_->HasCycle();
}

template <typename Node, typename CompareNode>
int CircularityDetector<Node, CompareNode>::GetComponentId(const Node& node) {
  return impl_->GetComponentId(node);
}

#endif  // _CIRCULARITY_DETECTOR_INL_H__
