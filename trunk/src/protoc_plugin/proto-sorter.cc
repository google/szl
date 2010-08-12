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

// Copyright 2009 Google Inc.  All rights reserved.
// Author: mikejones@google.com (Mike Jones)
//
// This file contains functions for sorting the messages within a
// proto2::FileDescriptor instance so that, if message X depends on message Y,
// message Y appears first in the output .szl file: a requirement of Sawzall,
// but not of the .proto language.

#include <algorithm>
#include <map>
#include <set>

using namespace std;

#include "google/protobuf/stubs/common.h"

#include "protoc_plugin/proto-sorter.h"
#include "protoc_plugin/circularity_detector-inl.h"
#include "protoc_plugin/topologicalsorter-inl.h"

namespace google {
namespace protobuf {
namespace compiler {
namespace szl {
namespace sorter {

// Logs warning(s) identifying the names of mutually recursive types.
static void LogCycleWarning(Cycles &cycles, const vector<string> &names) {
  for (Cycles::const_iterator it = cycles.begin(); it != cycles.end(); it++) {
    string warning("Compiled Sawzall types are mutually recursive:");
    for (Nodes::const_iterator n = it->begin(); n != it->end(); n++) {
      warning += string(" ") + names[*n];
    }
    GOOGLE_LOG(WARNING) << warning;
  }
}

// Recursively adds a tag's dependencies to nodes.
static void AddDescriptorDependencies(const Descriptor& descriptor,
                                      const vector<string>& type_names,
                                      Nodes* nodes) {
  for (int i = 0; i < descriptor.field_count(); i++) {
    const FieldDescriptor &field(*(descriptor.field(i)));
    if (field.type() == FieldDescriptor::TYPE_GROUP) {
      // Add all of the dependencies within the group.
      AddDescriptorDependencies(*field.message_type(), type_names, nodes);
    }
    if (field.type() == FieldDescriptor::TYPE_MESSAGE) {
      // Look up the named type's index and add this to nodes.
      vector<string>::const_iterator it =
          find(type_names.begin(), type_names.end(),
               field.message_type()->full_name());
      if (it != type_names.end()) {
        nodes->insert(it - type_names.begin());
      }
    }
  }
}

void EnsureTopologicallySorted(const FileDescriptor& file,
                               vector<const Descriptor*>* sorted_messages) {
  DependencyMap dependency_map;
  vector<string> type_names;
  GetDependencyMap(file, &dependency_map, &type_names);
  if (!IsTopologicallySorted(dependency_map)) {
    string warning;
    Cycles circular_dependencies;
    GetCircularDependencies(dependency_map, &circular_dependencies);
    if (circular_dependencies.size() > 0) {
      // We log a warning, don't attempt to sort, but fall through to fill in
      // the sorted_messages vector: the Sawzall file we output will contain
      // recursive type dependencies and won't compile directly, but it's just
      // possible a downstream tool could still make some use of it.
      LogCycleWarning(circular_dependencies, type_names);
    } else {
      SortTopologically(file, dependency_map, sorted_messages);
      return;
    }
  }

  // The existing messages are already topologically sorted, but we still
  // need to fill in the elements of sorted_messages.
  for (int i = 0; i < file.message_type_count(); i++) {
    sorted_messages->push_back(file.message_type(i));
  }
}

void GetDependencyMap(const FileDescriptor& file,
                      DependencyMap* dependency_map,
                      vector<string> *type_names) {
  for (int i = 0; i < file.message_type_count(); i++) {
    type_names->push_back(file.message_type(i)->full_name());
  }

  for (int i = 0; i < file.message_type_count(); i++) {
    Nodes nodes;
    AddDescriptorDependencies(*file.message_type(i), *type_names, &nodes);

    // Remove self-recursive dependency if one exists.
    Nodes::iterator it = find(nodes.begin(), nodes.end(), i);
    if (it != nodes.end()) {
      nodes.erase(it);
    }
    if (!nodes.empty()) {
      (*dependency_map)[i] = nodes;
    }
  }
}

// Returns true if for every n, each node in dependency_map[n] is <= n.
bool IsTopologicallySorted(const DependencyMap& dependency_map) {
  for (DependencyMap::const_iterator map_it = dependency_map.begin();
       map_it != dependency_map.end();
       map_it++) {
    for (Nodes::const_iterator it = map_it->second.begin();
         it != map_it->second.end();
         it++) {
      if (*it > map_it->first) {
        return false;
      }
    }
  }
  return true;
}

void GetCircularDependencies(const DependencyMap& dependency_map,
                             Cycles* cycles) {
  CircularityDetector<int> cycle_detector;

  for (DependencyMap::const_iterator map_it = dependency_map.begin();
       map_it != dependency_map.end();
       map_it++) {
    for (Nodes::const_iterator it = map_it->second.begin();
         it != map_it->second.end();
         it++) {
      cycle_detector.AddEdge(*it, map_it->first);
    }
  }

  if (!cycle_detector.HasCycle()) {
    return;
  }

  // Map, strongly connected component ID -> set of node indices.
  map<int, Nodes> id_map;
  for (DependencyMap::const_iterator map_it = dependency_map.begin();
       map_it != dependency_map.end();
       map_it++) {
    int node = map_it->first;
    id_map[cycle_detector.GetComponentId(node)].insert(node);
  }
  for (map<int, Nodes>::const_iterator it = id_map.begin();
       it != id_map.end();
       it++) {
    const Nodes &nodes(it->second);
    if (nodes.size() > 1) {
      cycles->insert(nodes);
    }
  }
}

void SortTopologically(const FileDescriptor& file,
                       const DependencyMap &dependency_map,
                       vector<const Descriptor*> *sorted_messages) {
  TopologicalSorter<int> sorter;

  // Add a node for every message even if no edges connect to it to ensure
  // that every message is output.
  for (int i = 0; i < file.message_type_count(); i++) {
    sorter.AddNode(i);
  }

  // Add edges to sorter from dependency_map.
  for (DependencyMap::const_iterator map_it = dependency_map.begin();
       map_it != dependency_map.end();
       map_it++) {
    for (Nodes::const_iterator it = map_it->second.begin();
         it != map_it->second.end();
         it++) {
      sorter.AddEdge(*it, map_it->first);
    }
  }

  // Perform the sort.
  int node;
  bool cyclic = false;
  while (sorter.GetNext(&node, &cyclic)) {
    sorted_messages->push_back(file.message_type(node));
  }
  assert(!cyclic);
}

}  // namespace sorter
}  // namespace szl
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
