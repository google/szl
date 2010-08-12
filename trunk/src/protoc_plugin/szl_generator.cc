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

// This module outputs Sawzall tuple for protocol messsage.

#include <utility>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

using namespace std;

#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/io/printer.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/zero_copy_stream.h"


#include "protoc_plugin/proto-sorter.h"
#include "protoc_plugin/szl_generator.h"
#include "protoc_plugin/linked_ptr.h"
#include "protoc_plugin/strutil.h"


// Protocol buffer types and data are represented as follows:
//
// Messages are emitted as tuple type declarations.
// Groups are emitted as tuple type declarations.
// Enum types are emitted as int type declarations.
// Enum values are emitted as static declarations with type int.
// For messages only, the mapping from tag numbers to tag names is emitted
//   as an array of strings with the (qualified) name "tagnames".
//   The string for tag 0 is "ErrorCode"; the string for any other unused tag
//   number is "None".
// For each enum type, the mapping from values to names is emitted as a map
//   with the (qualified) name formed by appending "_names" to the enum name.
//
// The format places all declarations, including metadata, within the
// tuple representing the enclosing group or message.  The only global names
// are message names, which are qualified with any package names.  All
// non-global names are unqualified.
// Foreign message names are qualified using dot as a delimiter.
// If an enum type name matches a field name or enum value name, an underscore
// is appended to the type name.
//
// Compatibility issues:
// - When an enum type name, enum value name, message name, group name or
//   field name matches a Sawzall keyword, generator always appends an
//   underscore to the name.
// - When an enum type name matches a field name or enum value name,
//   the generator appends an underscore.
// - The name "tagnames" and the names formed by appending "_names" to the
//   enum type names may conflict with field names.

namespace google {
namespace protobuf {
namespace compiler {
namespace szl {

namespace {

// see scanner.h
// List of proper keywords and Sawzall basic types.
static const char* const keywords[] = {
  // scanner.h defines a predicate IsKeyword()
  // that will be true for these identifers - we could use
  // that function but we don't because we
  // don't want to introduce more BUILD dependencies.
  // We consider Sawzall basic types as
  // keywords for the purpose of the
  // protocol-compiler so that they don't accidentally
  // become inaccessible because of equally named
  // variables or fields in a .proto file.
  "all",
  "and",
  "array",
  "bool",
  "break",
  "bytes",
  "case",
  "continue",
  "default",
  "do",
  "each",
  "else",
  "emit",
  "file",
  "fingerprint",
  "float",
  "for",
  "format",
  "function",
  "if",
  "include",
  "job",
  "int",
  "map",
  "merge",
  "mill",
  "millmerge",
  "not",
  "of",
  "or",
  "parsedmessage",
  "pipeline",
  "proc",
  "proto",
  "rest",
  "return",
  "skip",
  "some",
  "static",
  "string",
  "submatch",
  "switch",
  "table",
  "time",
  "type",
  "weight",
  "when",
  "while"
};
const int kNumKeywords = sizeof(keywords) / sizeof(keywords[0]);

static set<string> mapped_names;


// Comparison function
struct SzlStrlt : public binary_function<const char*, const char*, bool> {
  bool operator()(const char* s1, const char* s2) const {
    return (s1 != s2) && (s2 == 0 || (s1 != 0 && strcmp(s1, s2) < 0));
  }
};



// Dump list of remapped names.
static void WarnAboutRenames() {
  string s;
  for (set<string>::iterator i = mapped_names.begin();
      i != mapped_names.end();
      ++i) {
    s.push_back(' ');
    s.append(*i);
  }
  if (!mapped_names.empty()) {
    fprintf(stderr, "protocol-compiler: warning: these names conflict with "
                    "sawzall keywords or other uses; some instances may have "
                    "an underscore appended:%s\n", s.c_str());
  }
}

// Name, making sure not to use a keyword
static string NonKeyWord(const string& s) {
  if (binary_search(&keywords[0], &keywords[kNumKeywords],
                    s.c_str(), SzlStrlt())) {
    mapped_names.insert(s);
    return s + "_";
  }
  return s;
}

// Dotted name, making sure not to use a keyword in any segment
// This is fairly inefficient, but we don't do it very often.
static string DottedNonKeyWord(const string& s) {
  string result;
  const char* segment = s.c_str();
  while (true) {
    const char* dot = strchr(segment, '.');
    if (dot == NULL) {
      result.append(NonKeyWord(segment));
      return result;
    }
    result.append(NonKeyWord(string(segment, dot - segment)));
    result.append(".");
    segment = dot + 1;
  }
}

static bool HasSawzallType(const FieldDescriptor& field) {
  switch (field.type()) {
    case FieldDescriptor::TYPE_DOUBLE:
    case FieldDescriptor::TYPE_FLOAT:
    case FieldDescriptor::TYPE_INT64:
    case FieldDescriptor::TYPE_UINT64:
    case FieldDescriptor::TYPE_INT32:
    case FieldDescriptor::TYPE_FIXED64:
    case FieldDescriptor::TYPE_FIXED32:
    case FieldDescriptor::TYPE_BOOL:
    case FieldDescriptor::TYPE_STRING:
    case FieldDescriptor::TYPE_GROUP:
    case FieldDescriptor::TYPE_MESSAGE:
    case FieldDescriptor::TYPE_BYTES:
    case FieldDescriptor::TYPE_UINT32:
    case FieldDescriptor::TYPE_ENUM:
    case FieldDescriptor::TYPE_SFIXED32:
    case FieldDescriptor::TYPE_SFIXED64:
    case FieldDescriptor::TYPE_SINT32:
    case FieldDescriptor::TYPE_SINT64:
      return true;
  }
  return false;
}

// Returns a literal giving the default value for a field.
// If the field specifies no explicit default value, we'll return
// the default value for the field type (zero for numbers,
// empty string for strings, empty list for repeated fields, and
// empty bytes for non-repeated, composite fields).
string StringifyDefaultValue(const FieldDescriptor& field) {
  if (field.is_repeated()) {
    return "[]";
  }
  switch (field.cpp_type()) {
    case FieldDescriptor::CPPTYPE_INT32:
      return SimpleItoa(field.default_value_int32());
    case FieldDescriptor::CPPTYPE_UINT32:
      return SimpleItoa(field.default_value_uint32()) + "U";
    case FieldDescriptor::CPPTYPE_INT64:
      return SimpleItoa(field.default_value_int64());
    case FieldDescriptor::CPPTYPE_UINT64:
      return SimpleItoa(field.default_value_uint64()) + "U";
    case FieldDescriptor::CPPTYPE_DOUBLE: {
      string double_str = SimpleDtoa(field.default_value_double());
      if (double_str.find(".") == string::npos &&
          double_str.find("e") == string::npos) {
        double_str += ".0";
      }
      return double_str;
    }
    case FieldDescriptor::CPPTYPE_FLOAT: {
      string float_str = SimpleFtoa(field.default_value_float());
      if (float_str.find(".") == string::npos &&
          float_str.find("e") == string::npos) {
        float_str += ".0";
      }
      return float_str;
    }
    case FieldDescriptor::CPPTYPE_BOOL:
      return field.default_value_bool() ? "true" : "false";
    case FieldDescriptor::CPPTYPE_ENUM:
      return SimpleItoa(field.default_value_enum()->number());
    case FieldDescriptor::CPPTYPE_STRING:
      if (field.type() == FieldDescriptor::TYPE_BYTES) {
        return "B\"" + CEscape(field.default_value_string()) + "\"";
      } else {
        return "\"" + CEscape(field.default_value_string()) + "\"";
      }
    case FieldDescriptor::CPPTYPE_MESSAGE:
      return "B\"\"";
  }
  // (We could add a default case above but then we wouldn't get the nice
  // compiler warning when a new type is added.)
  GOOGLE_LOG(FATAL) << "Not reached.";
  return "";
}
}  // namespace


SzlGenerator::SzlGenerator()
    : file_(NULL), sawzall_suppress_warnings_(false) {
}

SzlGenerator::~SzlGenerator() {
}

bool SzlGenerator::Generate(const FileDescriptor* file,
                         const string& parameter,
                         compiler::OutputDirectory* output_directory,
                         string* error) const {
  // Completely serialize all Generate() calls on this instance.  The
  // thread-safety constraints of the CodeGenerator interface aren't clear so
  // just be as conservative as possible.  It's easier to relax this later if
  // we need to, but I doubt it will be an issue.
  // TODO:  The proper thing to do would be to allocate any state on
  //   the stack and use that, so that the Generator class itself does not need
  //   to have any mutable members.  Then it is implicitly thread-safe.
  MutexLock lock(&mutex_);
  file_ = file;
  const char* suffix = HasSuffixString(file->name(), ".protodevel")
      ? ".protodevel" : ".proto";
  string filename = StripSuffixString(file->name(), suffix) + ".szl";

  linked_ptr<io::ZeroCopyOutputStream> output(output_directory->Open(filename));
  GOOGLE_CHECK(output.get());
  io::Printer printer(output.get(), '$');
  printer_ = &printer;

  PrintHeader(file->name());
  PrintImports();
  PrintTopLevelEnums();
  PrintTopLevelExtensions();
  PrintMessages();
  if (!sawzall_suppress_warnings_) {
    WarnAboutRenames();
  }
  return !printer.failed();
}

// Print header for the file being created.
void SzlGenerator::PrintHeader(const string& filename) const {
  printer_->Print("# This file automatically generated by protocol-compiler\n"
                  "# from $filename$\n"
                  "# DO NOT EDIT!\n\n",
                  "filename", filename);
}

// Print imports for files.
void SzlGenerator::PrintImports() const {
  for (int i = 0; i < file_->dependency_count(); ++i) {
    string filename = file_->dependency(i)->name();
    // sawzall_message_set.proto will allow proto containing MessageSet to be
    // processed with Sawzall. For details, refer to the comments in the file.
    if (filename == "net/proto2/bridge/proto/message_set.proto") {
      filename = "net/proto/sawzall_message_set.proto";
    }
    printer_->Print("proto \"$filename$\"\n",
                    "filename", filename);
  }
}

// Print top-level extensions.
void SzlGenerator::PrintTopLevelExtensions() const {
  // TODO: Implement
}

// In Sawzall, we emit proto2 top-level enums as dummy proto messages with the
// same name as the enum and containing only the enum. We do this by creating
// a fake proto message and emitting it directly.
void SzlGenerator::PrintTopLevelEnums() const {
  vector<pair<string, int> > top_level_enum_values;
  for (int i = 0; i < file_->enum_type_count(); ++i) {
    const EnumDescriptor& enum_descriptor = *file_->enum_type(i);
    printer_->Print("type $name$ = parsedmessage {\n",
                    "name", enum_descriptor.full_name());
    printer_->Indent();
    set<string> nontype_names;
    RenamedEnums renamed_enums;
    PrintEnum(enum_descriptor, nontype_names, &renamed_enums);
    printer_->Outdent();
    printer_->Print("};\n");
  }
}

void SzlGenerator::PrintEnum(
    const EnumDescriptor& enum_descriptor,
    const set<string> nontype_names,
    RenamedEnums* renamed_enums) const {
  string type_name = NonKeyWord(enum_descriptor.name());
  if (nontype_names.find(type_name) != nontype_names.end()) {
    renamed_enums->insert(&enum_descriptor);
    mapped_names.insert(type_name);
    type_name += "_";
  }
  printer_->Print("type $type_name$ = int,\n\n", "type_name", type_name);

  string enum_value_name;
  for (int i = 0; i < enum_descriptor.value_count(); ++i) {
    const EnumValueDescriptor* enum_value = enum_descriptor.value(i);
    map<string, string> m;
    m["enum_value_name"] = NonKeyWord(enum_value->name());
    m["type_name"] = type_name;
    m["enum_number"] = SimpleItoa(enum_value->number());
    printer_->Print(m,
                    "static $enum_value_name$: $type_name$ = $enum_number$,\n");
  }
  string map_name = enum_descriptor.name() + "_names";
  PrintEnumValueMap(enum_descriptor, map_name);
}

// Print the map from enum int value to enum name string
// If an enum value has more than one name, we only print the last name.
void SzlGenerator::PrintEnumValueMap(
    const EnumDescriptor& enum_descriptor, const string& map_name) const {
  printer_->Print("static $map_name$: "
                  "map[enum_value: int] of enum_name: string = {\n",
                  "map_name", map_name);
  printer_->Indent();
  set<int> enum_values_printed;
  for (int i = enum_descriptor.value_count() - 1; i >= 0 ; --i) {
    const EnumValueDescriptor* enum_value = enum_descriptor.value(i);
    int value = enum_value->number();
    if (enum_values_printed.find(value) == enum_values_printed.end()) {
      enum_values_printed.insert(value);
      printer_->Print("$enum_number$: \"$enum_name$\",\n",
                      "enum_number", SimpleItoa(value),
                      "enum_name", enum_value->name());
    }
  }
  printer_->Outdent();
  printer_->Print("},\n");
}

// Collect field and enum value names.
void SzlGenerator::CollectLocalNames(
  const Descriptor& descriptor, set<string>* nontype_names) const {
  for (int i = 0; i < descriptor.field_count(); i++) {
    const FieldDescriptor* field = descriptor.field(i);
    if (HasSawzallType(*field)) {
      nontype_names->insert(NonKeyWord(field->name()));
    }
  }
  for (int i = 0; i < descriptor.enum_type_count(); i++) {
    const EnumDescriptor* e = descriptor.enum_type(i);
    for (int j = 0; j < e->value_count(); ++j) {
      const EnumValueDescriptor* e_val = e->value(j);
      nontype_names->insert(NonKeyWord(e_val->name()));
    }
  }
}

// Recursively print enums in nested types within descriptor, then
// print enums contained at the top level in descriptor.
void SzlGenerator::PrintEnums(
    const Descriptor& descriptor, RenamedEnums* renamed_enums) const {
  // For the local case, first build a set of the names we must avoid.
  set<string> nontype_names;
  CollectLocalNames(descriptor, &nontype_names);
  for (int i = 0; i < descriptor.enum_type_count(); ++i) {
    PrintEnum(*descriptor.enum_type(i), nontype_names, renamed_enums);
  }
}

// Prints all messages in file.
void SzlGenerator::PrintMessages() const {
  vector<const Descriptor*> sorted_messages;
  sorter::EnsureTopologicallySorted(*file_, &sorted_messages);
  for (int i = 0; i < sorted_messages.size(); ++i) {
    PrintMessage(*sorted_messages[i], 0);
    printer_->Print("\n");
  }
}

void SzlGenerator::PrintMessage(
    const Descriptor& message_descriptor, int depth) const {
  // Print the class, emitting groups and enums as tuple-local declarations.
  RenamedEnums renamed_enums;
  PrintClass(message_descriptor, depth, &renamed_enums);
}

// Mutually recursive with PrintMessage().
void SzlGenerator::PrintNestedMessages(
    const Descriptor& containing_descriptor, int depth) const {
  for (int i = 0; i < containing_descriptor.nested_type_count(); ++i) {
    printer_->Print("\n");
    PrintMessage(*containing_descriptor.nested_type(i), depth + 1);
  }
}

void SzlGenerator::PrintClass(
    const Descriptor& message_descriptor,
    int depth,
    RenamedEnums* renamed_enums) const {
  string type = (depth == 0) ? "parsedmessage " : "";
  string name;
  name = (depth == 0) ?
      message_descriptor.full_name() : message_descriptor.name();
  printer_->Print("type $name$ = $type${\n", "name", name, "type", type);
  printer_->Indent();
  PrintEnums(message_descriptor, renamed_enums);
  if (depth == 0) {
    PrintTags(message_descriptor);
  }
  PrintExtensions(message_descriptor);
  PrintNestedMessages(message_descriptor, depth + 1);
  PrintFields(message_descriptor, renamed_enums);
  printer_->Outdent();
  printer_->Print("}$declterm$\n", "declterm", depth == 0 ? ";" : ",");
}

// Print the array mapping tag numbers to tag names (for messages only).
void SzlGenerator::PrintTags(const Descriptor& descriptor) const {
  int num_tags = descriptor.field_count();
  for (int i = 0; i < descriptor.field_count(); i++) {
    const FieldDescriptor* field = descriptor.field(i);
    if (field->type() == FieldDescriptor::TYPE_GROUP) {
      num_tags += field->message_type()->field_count();
    }
  }
  if (num_tags < 2) return;

  printer_->Print("static tagnames: map[string] of int = {\n");
  printer_->Indent();
  printer_->Print("\"ErrorCode\": 0,\n");
  int max_tag = 0;
  map<int, string> tag_mapping;
  for (int i = 0; i < descriptor.field_count(); i++) {
    const FieldDescriptor* field = descriptor.field(i);
    GetTagNameMapping(*field, &max_tag, &tag_mapping);
  }
  map<string, string> m;
  for (int i = 1; i <= max_tag; i++) {
    if (tag_mapping.find(i) != tag_mapping.end()) {
      m["field_name"] = tag_mapping[i];
      m["separator"] = (i == max_tag) ? "" : ",";
      m["id"] = SimpleItoa(i);
      printer_->Print(m, "\"$field_name$\": $id$$separator$\n");
    }
  }
  printer_->Outdent();
  printer_->Print("},\n");
}

void SzlGenerator::GetTagNameMapping(
    const FieldDescriptor& field, int* max_tag, map<int, string>* tag_mapping)
    const {
  if (field.number() > *max_tag) {
    *max_tag = field.number();
  }
  string field_name = field.type() == FieldDescriptor::TYPE_GROUP ?
      field.message_type()->name() : field.name();
  tag_mapping->insert(make_pair(field.number(), field_name));
  if (field.type() == FieldDescriptor::TYPE_GROUP) {
    const Descriptor* group = field.message_type();
    for (int j = 0; j < group->field_count(); ++j) {
      GetTagNameMapping(*group->field(j), max_tag, tag_mapping);
    }
  }
}

// Print the contents of a tuple.
void SzlGenerator::PrintFields(
    const Descriptor& descriptor, RenamedEnums* renamed_enums) const {
  // Print elements of tuple locally in class; {} printed by caller
  for (int i = 0; i < descriptor.field_count(); i++) {
    const FieldDescriptor* field = descriptor.field(i);
    if (!HasSawzallType(*field))
      continue;
    string field_name = field->name();
    LowerString(&field_name);
    printer_->Print("$field_name$:$array_decl$\n",
                    "field_name", NonKeyWord(field_name),
                    "array_decl", field->is_repeated() ? " array of" : "");
    PrintType(*field,
              (i < descriptor.field_count() - 1) ? "," : "", renamed_enums);
  }
}

// Print the Sawzall type for a tag.
void SzlGenerator::PrintType(const FieldDescriptor& field,
                          const char* comma,
                          RenamedEnums* renamed_enums) const {
  string szl_type = "UNKNOWN";
  string proto_type = "";

  switch (field.type()) {
    case FieldDescriptor::TYPE_DOUBLE:
      szl_type = "float";
      proto_type = ": double";
      break;
    case FieldDescriptor::TYPE_FLOAT:
      szl_type = "float";
      proto_type = ": float";
      break;
    case FieldDescriptor::TYPE_INT64:
      szl_type = "int";
      proto_type = ": int64";
      break;
    case FieldDescriptor::TYPE_UINT64:
      szl_type = "uint";
      proto_type = ": uint64";
      break;
    case FieldDescriptor::TYPE_INT32:
      szl_type = "int";
      proto_type = ": int32";
      break;
    case FieldDescriptor::TYPE_FIXED64:
      szl_type = "uint";
      proto_type = ": uint64";
      break;
    case FieldDescriptor::TYPE_FIXED32:
      szl_type = "uint";
      proto_type = ": fixed32";
      break;
    case FieldDescriptor::TYPE_BOOL:
      szl_type = "bool";
      proto_type = ": bool";
      break;
    case FieldDescriptor::TYPE_STRING:
      szl_type = "string";
      proto_type = ": string";
      break;
    case FieldDescriptor::TYPE_BYTES:
      szl_type = "bytes";
      proto_type = ": bytes";
      break;
    case FieldDescriptor::TYPE_UINT32:
      szl_type = "uint";
      proto_type = ": uint32";
      break;
    case FieldDescriptor::TYPE_SFIXED32:
      szl_type = "int";
      proto_type = ": int32";
      break;
    case FieldDescriptor::TYPE_SFIXED64:
      szl_type = "int";
      proto_type = ": int64";
      break;
    case FieldDescriptor::TYPE_SINT32:
      szl_type = "int";
      proto_type = ": int32";
      break;
    case FieldDescriptor::TYPE_SINT64:
      szl_type = "int";
      proto_type = ": int64";
      break;
    case FieldDescriptor::TYPE_ENUM:
      szl_type = StripPrefixString(field.enum_type()->full_name(),
                                   field.containing_type()->full_name() + ".");
      // Assign type as int for top level enum.
      // Type name can't be used here as top level enum defintion is
      // generated as a tuple.
      if (field.enum_type()->containing_type() == NULL) {
        szl_type = "int";
      }
      proto_type = ": int32";
      break;
    case FieldDescriptor::TYPE_GROUP: {
      const Descriptor* group = field.message_type();
      if (group->field_count() == 0) {
        szl_type = "{}";
      } else {
        szl_type = group->name();
      }
      break;
    }
    case FieldDescriptor::TYPE_MESSAGE: {
      const Descriptor* message = field.message_type();
      // Anything with message_set_wire_format is replaced with the core
      // MessageSet class in proto1.
      // Refer to proto_compiler::CompilerDowngrader::Downgrade.
      if (!message->options().message_set_wire_format()) {
        szl_type = DottedNonKeyWord(message->full_name());
      } else {
        szl_type = "MessageSet";
      }
      break;
    }
    default:
      // TODO: Implement unsupported types.
      GOOGLE_LOG(WARNING) << "Type is not yet supported in Sawzall: " << field.type();
      return;
  }
  string default_init = field.has_default_value() ?
      " = " + StringifyDefaultValue(field) : "";
  printer_->Indent();
  map<string, string> m;
  m["szl_type"] = szl_type;
  m["default_init"] = default_init;
  m["id"] = SimpleItoa(field.number());
  m["proto_type"] = proto_type;
  m["comma"] = comma;
  printer_->Print(m, "$szl_type$$default_init$ @ $id$$proto_type$$comma$\n");
  printer_->Outdent();
}

void SzlGenerator::PrintExtensions(const Descriptor& message_descriptor) const {
  for (int i = 0; i < message_descriptor.extension_count(); ++i) {
    const FieldDescriptor* extension_field = message_descriptor.extension(i);
    // TODO: Implement support for all the extensions.
    if (extension_field->containing_type()->full_name() ==
        "proto2.bridge.MessageSet") {
      PrintMessageSetExtensions(*extension_field);
    }
  }
}

void SzlGenerator::PrintMessageSetExtensions(const FieldDescriptor& field) const {
  printer_->Print("type TypeId = int,\n");
  printer_->Print("static MESSAGE_TYPE_ID: TypeId = $number$,\n",
                  "number", SimpleItoa(field.number()));
  printer_->Print("static TypeId_names: map[enum_value: int] "
                  "of enum_name: string = {\n");
  printer_->Indent();
  printer_->Print("$number$: \"MESSAGE_TYPE_ID\",\n",
                  "number", SimpleItoa(field.number()));
  printer_->Outdent();
  printer_->Print("},\n");
}

}  // namespace szl
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
