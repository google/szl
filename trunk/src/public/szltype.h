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

// This is one of a small number of top-level header files for the Sawzall
// component.  See sawzall.h for a complete list.  No other lower-level
// header files should be included by clients of the Sawzall implementation.

#ifndef _PUBLIC_SZLTYPE_H__
#define _PUBLIC_SZLTYPE_H__

#include <string>
#include <vector>

namespace sawzall {
  class SzlTypeProto;
  class SzlFieldProto;
}

using sawzall::SzlTypeProto;
using sawzall::SzlFieldProto;


class SzlField;

// Type information for Sawzall output values stored in SzlValue objects.

class SzlType {
 public:

  // These values must be kept in sync with those in sawzall.proto
  enum Kind {
    VOID = 0,

    // Base types: only kind is valid; no param, element, fields, etc.
    BOOL = 1,
    BYTES = 2,
    FINGERPRINT = 3,
    FLOAT = 4,
    INT = 5,
    STRING = 6,
    TIME = 7,

    TUPLE = 8,      // has fields
    ARRAY = 9,      // has (unlabelled) element
    MAP = 10,

    // Table types
    // All have element and optional indices.
    TABLE = 11,

    // Function type - not used
    FUNCTION = 12,

    NKIND = 13
  };

  // Properties of different table types;
  struct TableProperties {
    const char* name;
    bool        has_param;
    bool        has_weight;
  };

  explicit SzlType(Kind kind);
  //explicit SzlType(const string& typestr);

  // copy constructor and assignment
  SzlType(const SzlType &type);
  SzlType & operator=(const SzlType& type);
  void PartialReset();

  // Decode/Encode between SzlType and protocol buffer.
  bool ParseFromSzlArray(const char* buf, int len, string* error);
  bool InitFromSzlProto(const SzlTypeProto& tpb, string* error);

  ~SzlType();

  // Handy instances of base types.
  static const SzlType kVoid;
  static const SzlType kBool;
  static const SzlType kBytes;
  static const SzlType kFingerprint;
  static const SzlType kFloat;
  static const SzlType kInt;
  static const SzlType kString;
  static const SzlType kTime;

  // Accesors for kind, table, and param.
  Kind kind() const { return kind_; }
  const string& table() const { return table_; }
  int param() const { return param_; }

  // Set the kind and param.
  void set_kind(Kind kind);
  void set_table(const string& table);
  void set_param(int param);

  // Accessors for the element and weight fields.
  const SzlField* element() const  { return element_; }
  const SzlField* weight() const  { return weight_; }
  bool has_weight() const  { return weight_ != NULL; }

  // Set the element or weight field.
  // A copy of field is made, and destroyed with the type or when overwritten.
  void set_element(const SzlField* field);
  void set_weight(const SzlField* field);

  // Same as set_element(field) / set_weight(field), but no need to first
  // construct a SzlField and hence more convenient for unittests and such.
  void set_element(const string &label, const SzlType &type);
  void set_weight(const string &label, const SzlType &type);

  // Accessors for examining the indices of a table.
  int indices_size() const;
  void indices_resize(int size);
  const SzlField& index(int i) const;

  // Append an index to the type.
  void AddIndex(const string &label, const SzlType &type);

  // Accessors for examining the fields of a tuple.
  int fields_size() const;
  void fields_resize(int size);
  const SzlField& field(int i) const;

  // Append a field to the type.
  void AddField(const string &label, const SzlType &type);

  // Checks for classes of types.
  bool BadType() const;
  bool BaseType() const;
  bool TupleType() const;
  bool TableType() const;

  // Like BaseType(), but based on specified kind.
  static bool BaseKind(Kind kind);

  // Is this type a well constructed type?
  bool Valid(string* error) const;

  // Structural comparison.
  bool Equal(const SzlType& type) const;

  // Pretty printing.
  string PPrint() const;

  // Return the name of the kind.
  static string KindName(Kind kind);

  friend ostream &operator<<(ostream&, const SzlType&);

 private:
  bool ValidIndices(string* error) const;

  Kind kind_;
  string table_;    // if kind_ == TABLE, the name of the table kind.
  int param_;

  mutable bool valid_;  // cache for Valid()

  vector<SzlField>* fields_;
  vector<SzlField>* indices_;
  SzlField* element_;
  SzlField* weight_;
};

// A field in a SzlType.
class SzlField {
 public:
  SzlField()
    : type_(SzlType::VOID) {}
  SzlField(const string &l, const SzlType &t)
    : label_(l), type_(t) {}

  const string& label() const  { return label_; }
  const SzlType& type() const  { return type_; }

 private:
  friend class SzlType;
  // Decode/Encode between SzlType and protocol buffer.
  bool ParseFromArray(const char* buf, int len, string* error);
  bool InitFromProto(const SzlFieldProto &fpb, string* error);
  bool InitFromSzlProto(const SzlFieldProto &fpb, string* error);

  // These fields are logically const but need to be non-const so they
  // can be copied by operator=.
  string label_;
  SzlType type_;
};

#endif /* _PUBLIC_SZLTYPE_H__ */
