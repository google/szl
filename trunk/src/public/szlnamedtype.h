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

#ifndef _PUBLIC_SZLNAMEDTYPE_H__
#define _PUBLIC_SZLNAMEDTYPE_H__

#include <string>

#include "public/szltype.h"


// This class is a thin wrapper around SzlType and acts like a SzlField. That
// is, the class contains both a type and a name (potentially empty). The main
// advantage of this wrapper is that it allows complex szl types to be
// constructed relatively concisely and in a more readable manner than is
// possible with the standard SzlType and SzlField classes. For example:
//
//   table sum[url: string] of { array of int, z: map[float] of string };
//
// becomes
//
//   SzlNamedTable("sum").Index(SzlNamedString("url")).Of(
//     SzlNamedTuple()
//         .Field(SzlNamedArray().Of(SzlNamedInt()))
//         .Field(SzlNamedMap("z").Index(SzlNamedFloat()).Of(SzlNamedString()))
//
class SzlNamedType {
 public:
  // Constructors. Note that the default copy constructor and assignment
  // operator are also available.
  explicit inline SzlNamedType(SzlType::Kind kind) : type_(kind) {
  }

  explicit inline SzlNamedType(SzlType type) : type_(type) {
  }

  inline SzlNamedType(SzlType::Kind kind, const string& name)
      : type_(kind), name_(name) {
  }

  inline SzlNamedType(SzlType type, const string& name)
      : type_(type), name_(name) {
  }

  virtual ~SzlNamedType() {
  }

  // These functions correspond to the SzlType equivalents (slightly different
  // naming, but it should be obvious). The main difference is that they
  // generally take named types and return a reference to the object itself.
  inline SzlNamedType& Field(const SzlNamedType& field) {
    type_.AddField(field.name_, field.type_);
    return *this;
  }

  inline SzlNamedType& Index(const SzlNamedType& index) {
    type_.AddIndex(index.name_, index.type_);
    return *this;
  }

  inline SzlNamedType& Weight(const SzlNamedType& weight) {
    type_.set_weight(weight.name_, weight.type_);
    return *this;
  }

  inline SzlNamedType& Param(int param) {
    type_.set_param(param);
    return *this;
  }

  // "Of" correponds to the element.
  inline SzlNamedType& Of(const SzlNamedType& element) {
    type_.set_element(element.name_, element.type_);
    return *this;
  }

  // Get the underlying SzlType.
  const SzlType& type() const {
    return type_;
  }

  // Get the name, potentially an empty string.
  const string& name() const {
    return name_;
  }

 protected:
  SzlType type_;
  string name_;
};


// The purpose of the type-specific classes below is to reduce the amount of
// typing and to make the resulting type construction look nicer.
template <SzlType::Kind kind>
class SzlNamedTypeT : public SzlNamedType {
 public:
  SzlNamedTypeT() : SzlNamedType(kind) {}
  SzlNamedTypeT(const string& name) : SzlNamedType(kind, name) {}
};

typedef SzlNamedTypeT<SzlType::VOID>        SzlNamedVoid;
typedef SzlNamedTypeT<SzlType::BOOL>        SzlNamedBool;
typedef SzlNamedTypeT<SzlType::BYTES>       SzlNamedBytes;
typedef SzlNamedTypeT<SzlType::FINGERPRINT> SzlNamedFingerprint;
typedef SzlNamedTypeT<SzlType::FLOAT>       SzlNamedFloat;
typedef SzlNamedTypeT<SzlType::INT>         SzlNamedInt;
typedef SzlNamedTypeT<SzlType::STRING>      SzlNamedString;
typedef SzlNamedTypeT<SzlType::TIME>        SzlNamedTime;
typedef SzlNamedTypeT<SzlType::TUPLE>       SzlNamedTuple;
typedef SzlNamedTypeT<SzlType::ARRAY>       SzlNamedArray;
typedef SzlNamedTypeT<SzlType::MAP>         SzlNamedMap;

// A table is different, because it has an argument, the type of the table.
class SzlNamedTable : public SzlNamedType {
 public:
  SzlNamedTable(const string& table) : SzlNamedType(SzlType::TABLE) {
    type_.set_table(table);
  }
  SzlNamedTable(const string& table, const string& name)
      : SzlNamedType(SzlType::TABLE, name) {
    type_.set_table(table);
  }
};

#endif  // _PUBLIC_SZLNAMEDTYPE_H__
