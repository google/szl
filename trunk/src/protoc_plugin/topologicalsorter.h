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

// TopologicalSorter provides topologically sorted traversal of
// the nodes of a directed acyclic graph. TopologicalSorter is
// templatized by the type used to identify nodes: typically
// a string or an integer id.
//
// Typical usage:
//   TopologicalSorter<string> sorter;
//   sorter.AddEdge("bar","baz");
//   sorter.AddNode("frob");
//   sorter.AddEdge("foo","bar");
//   string node;
//   bool cyclic = false;
//   while (sorter.GetNext(&node, &cyclic)) {
//     // visit nodes in topological order
//   }
//   GOOGLE_CHECK(!cyclic);
//
// TopologicalSorter requires that all nodes and edges be added
// before traversing the nodes, or else it dies with a fatal error.
//
// TopologicalSorter has not been tuned for performance, and
// is not currently thread-safe.
//
// TODO(plakal): perhaps move AddNode/AddEdge into a GraphBuilder class
// if there are other graph functions we can start adding in util/graph.

#ifndef _TOPOLOGICALSORTER_H__
#define _TOPOLOGICALSORTER_H__

#include <map>
#include <queue>
#include <vector>

template <typename T>
class TopologicalSorter {
 public:
  TopologicalSorter();
  ~TopologicalSorter();

  // Adds a node to the graph, if it has not
  // already been added via previous calls to
  // AddNode()/AddEdge(). If no edges are later
  // added connecting this node, then it remains
  // an isolated node in the graph. AddNode() only
  // exists to support isolated nodes. There is no
  // requirement (nor is it an error) to call AddNode()
  // for the endpoints used in a call to AddEdge().
  // Dies with a fatal error if called after a traversal
  // has been started with GetNext().
  void AddNode(const T& node);

  // Adds a directed edge with the given endpoints
  // to the graph. There is no requirement (nor is
  // it an error) to call AddNode() for the endpoints.
  // Dies with a fatal error if called after a traversal
  // has been started with GetNext().
  void AddEdge(const T& from, const T& to);

  // Visits the least node in topological order over
  // the current set of nodes and edges, and marks that
  // node as visited, so that repeated calls to GetNext()
  // will visit all nodes in order.
  // Writes the newly visited node in *node and returns
  // true with *cyclic set to false (assuming the graph
  // has not yet been discovered to be cyclic).
  // Returns false if all nodes have been visited, or if
  // the graph is discovered to be cyclic, in which case
  // *cyclic is also set to true.
  bool GetNext(T* node, bool* cyclic);

  // Returns a reference to a vector of nodes that currently
  // have zero indegree. The returned vector is owned by the
  // sorter and will change on the next call to GetNext().
  const vector<T> &GetCurrentFringe();

 private:
  // Have any calls to GetNext() been made? Used to
  // prevent edges being added during traversal.
  bool started_traversal_;

  // The directed edges added so far: a map from source node
  // to a list of destination nodes.
  map<T, vector<T> > edges_;

  // The indegree of each node. Zero-indegree nodes are
  // candidates for starting a topological sort. This is
  // updated during GetNext() to disregard edges from
  // visited nodes.
  map<T, int> indegrees_;

  // A queue of nodes that are currently the least in
  // topological order. Initialized when a traversal is started.
  queue<T> next_nodes_;

  // A vector of the most recent set of indegree 0 nodes.
  // Cleared after every call to GetNext().
  vector<T> fringe_;

  // The number of nodes returned so far in a traversal.
  int num_nodes_visited_;

  // Initializes the graph for traversal, if necessary.
  void InitTraversal();

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(TopologicalSorter);
};

#endif  // _TOPOLOGICALSORTER_H__
