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

// Algorithm to detect circular dependencies.

#ifndef _CIRCULARITY_DETECTOR_H__
#define _CIRCULARITY_DETECTOR_H__

#include <functional>        // for less<node>

// The class CircularityDetector implements an algorithm to detect circular
// dependencies in a directed graph.  The algorithm runs in time linear
// in the number of edges in the graph.  The expected usage is to build
// up the graph and then make queries.  If AddEdge is called between
// successive calls to IsCircular, the second call will traverse the
// entire graph and recompute the circularities.

// Internally, this class computes the strongly-connected
// components of the graph.  The algorithm used is a generalization of the
// one in topologicalsorter.h.  Given any directed graph, if each of its
// strongly-connected components is replaced by a single node, the resulting
// graph will be acyclic. The order in which the algorithm locates the SCCs
// constitutes a toplogical sort of the acyclic graph.  This SCC information
// can be obtained via GetComponentId().

// Node is any type that can be a key to map<>.  It must be be copyable
// and have an ordering function defined.  The default ordering function
// can be overridden by supplying a functors for CompareNode.
template <typename Node,
          typename CompareNode = less<Node> >
class CircularityDetector {
 public:
  // Creates an empty directed graph.
  CircularityDetector();
  ~CircularityDetector();

  // Adds an edge to the graph.
  void AddEdge(const Node &from, const Node &to);

  // Indicates whether there exists both a path from "left" to "right",
  // and also a path from "right" to "left".
  // IsCircular(x, x) returns True.
  bool IsCircular(const Node &left, const Node &right);

  // Indicates whether the graph contains at least one cycle.
  bool HasCycle();

  // Returns the strongly-connected component id of the given node.  Note
  // that this is only valid until the next time you add a node or an edge
  // to the graph, at which point all the component ids will have to
  // be recomputed, and the new component ids need not bear any
  // relationship to the old ones.
  int GetComponentId(const Node& node);

 private:
  class Impl;
  Impl* impl_;
  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(CircularityDetector);
};

#endif  // _CIRCULARITY_DETECTOR_H__
