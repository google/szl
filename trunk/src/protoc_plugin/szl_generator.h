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

// Copyright 2009 Google Inc. All Rights Reserved.
// Author: lianglin@google.com (Liang Lin)
//
// Adapted from the generator from Python by Will Robinson.
//
// Generates Python code for a given .proto file.


#ifndef _GENERATOR_H_
#define _GENERATOR_H_

#include <map>
#include <set>
#include <string>

#include "google/protobuf/compiler/code_generator.h"

namespace google {
namespace protobuf {

class Descriptor;
class EnumDescriptor;
class EnumValueDescriptor;
class FieldDescriptor;
class ServiceDescriptor;

namespace io { class Printer; }

namespace compiler {
namespace szl {

// Set of enums for which the type name was renamed by appending an
// underscore because it matched a field or enum value name.
typedef set<const EnumDescriptor*> RenamedEnums;

// CodeGenerator implementation for generated Sawzall protocol buffer classes.
// If you create your own protocol compiler binary and you want it to support
// Sawzall output, you can do so by registering an instance of this
// CodeGenerator with the CommandLineInterface in your main() function.
class SzlGenerator : public compiler::CodeGenerator {
 public:
  SzlGenerator();
  virtual ~SzlGenerator();

  // CodeGenerator methods.
  virtual bool Generate(const FileDescriptor* file,
                        const string& parameter,
                        compiler::OutputDirectory* output_directory,
                        string* error) const;

  // Whether to show the warning.
  void SuppressWarning(bool suppress) {
    sawzall_suppress_warnings_ = suppress;
  }

 private:
  void PrintHeader(const string& filename) const;
  void PrintImports() const;
  void PrintTopLevelExtensions() const;

  void PrintTopLevelEnums() const;
  void PrintEnums(
      const Descriptor& enum_descriptor, RenamedEnums* renamed_enums) const;
  void PrintEnum(
      const EnumDescriptor& enum_descriptor,
      const set<string> nontype_names,
      RenamedEnums* renamed_enums) const;
  void PrintEnumValueMap(
      const EnumDescriptor& enum_descriptor, const string& map_name) const;
  void CollectLocalNames(
      const Descriptor& descriptor, set<string>* nontype_names) const;

  void PrintMessages() const;
  void PrintMessage(const Descriptor& message_descriptor, int depth) const;
  void PrintNestedMessages(
      const Descriptor& containing_descriptor, int depth) const;

  void PrintClass(
      const Descriptor& message_descriptor,
      int depth,
      RenamedEnums* renamed_enums) const;
  void PrintTags(const Descriptor& descriptor) const;

  void GetTagNameMapping(
      const FieldDescriptor& field, int* max_tag, map<int, string>* tag_mapping)
      const;

  void PrintFields(
      const Descriptor& descriptor, RenamedEnums* renamed_enums) const;
  void PrintType(const FieldDescriptor& field,
                 const char* comma,
                 RenamedEnums* renamed_enums) const;

  void PrintExtensions(const Descriptor& message_descriptor) const;
  void PrintMessageSetExtensions(const FieldDescriptor& field) const;

  // Very coarse-grained lock to ensure that Generate() is thread-safe.
  // Guards file_ and printer_.
  mutable Mutex mutex_;
  mutable const FileDescriptor* file_;  // Set in Generate().  Under mutex_.
  mutable io::Printer* printer_;  // Set in Generate().  Under mutex_.
  const char* declterm_;  // the enum/group terminator; ";" or ","
  friend class GeneratorTest;
  bool sawzall_suppress_warnings_;
};

}  // namespace szl
}  // namespace compiler
}  // namespace protobuf
}  // namespace google

#endif  // _GENERATOR_H_
