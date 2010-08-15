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

// Functions for sorting the messages within a proto2::FileDescriptor instance
// so that, if message X depends on message Y, message Y appears first in the
// output file: a requirement of Sawzall, but not of the .proto language.

#ifndef _PROTO_SORTER_H__
#define _PROTO_SORTER_H__

#include <stdio.h>

#include <map>
#include <set>
#include <string>
#include <vector>

using namespace std;

#include "google/protobuf/descriptor.h"

namespace google {
namespace protobuf {
namespace compiler {
namespace szl {
namespace sorter {

// Nodes contains message indices using their original order within Proto::File.
typedef set<int> Nodes;
// Map from message index n to the message indices upon which message n depends.
typedef map<int, Nodes> DependencyMap;
// Cycles: sets of strongly connected nodes.
typedef set<Nodes> Cycles;

// Ensures that the messages within file are topogically sorted. This method
// does not modify file, but fills sorted_messages with pointers to message
// descriptors that are sorted topologically. Use sorted_messages only to
// iterate through the messages: you do not have ownership of the elements
// within and the contents will become invalid should file be changed.
// The algorithm is cautious, in that the order of the messages will match
// that of the original file unless achieving a valid ordering requires that
// they be moved.
void EnsureTopologicallySorted(const FileDescriptor& file,
                               vector<const Descriptor*>* sorted_messages);

// Extracts the dependencies between messages within file and the name of each
// message type. Names are qualified using dot-separated package specifiers.
// Self-recursive type dependencies are not added to dependency_map.
void GetDependencyMap(const FileDescriptor& file,
                      DependencyMap* dependency_map,
                      vector<string> *type_names);

// Returns true if dependency_map represents topologically sorted messages.
// That is, for each n, dependency_map[n] contains elements <= n.
bool IsTopologicallySorted(const DependencyMap& dependency_map);

// Returns sets of nodes where each elements within each set are strongly
// connected. A topological ordering is possible only if cycles is empty.
void GetCircularDependencies(const DependencyMap& dependency_map,
                             Cycles* cycles);

// Sorts messages topologically. It is a pre-condition that no mutually
// recursive message types exist. This should be confirmed via
// GetCircularDependencies() before calling this function.
// EnsureTopologicallySorted() provides a higher-level interface that
// will log a warning in this situation, but not attempt to sort the messages.
void SortTopologically(const FileDescriptor& file,
                       const DependencyMap &dependency_map,
                       vector<const Descriptor*> *sorted_messages);

}  // namespace sorter
}  // namespace szl
}  // namespace compiler
}  // namespace protobuf
}  // namespace google

#endif  // _PROTO_SORTER_H__
