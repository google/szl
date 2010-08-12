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

#include <string>
#include <vector>
#include <stdio.h>

#include "public/porting.h"
#include "public/logging.h"

#include "public/szltype.h"
#include "public/szlresults.h"

#include "emitvalues/sawzall.pb.h"

// handy instances of base types
const SzlType SzlType::kVoid(SzlType::VOID);
const SzlType SzlType::kBool(SzlType::BOOL);
const SzlType SzlType::kBytes(SzlType::BYTES);
const SzlType SzlType::kFingerprint(FINGERPRINT);
const SzlType SzlType::kFloat(SzlType::FLOAT);
const SzlType SzlType::kInt(SzlType::INT);
const SzlType SzlType::kString(SzlType::STRING);
const SzlType SzlType::kTime(SzlType::TIME);

namespace {

using sawzall::SzlTypeProto;
using sawzall::SzlFieldProto;

// Error for a protocol buffer for a type can't be parsed.
static const char* kBadTypeParse =
  "invalid or corrupted type description string";

// Error for a protocol buffer for a type has a kind we don't know about
static const char* kUnknownKind =
  "unknown type kind in type description string";

struct Tbl {
  Tbl();
  string kind_[SzlType::NKIND];
  bool base_[SzlType::NKIND];
};

static Tbl tbl;

}  // namespace

Tbl::Tbl() {
  for (int i = 0; i < SzlType::NKIND; ++i)
    kind_[i] = "unknown";

  kind_[SzlType::VOID] = "void";
  kind_[SzlType::BOOL] = "bool";
  kind_[SzlType::BYTES] = "bytes";
  kind_[SzlType::FINGERPRINT] = "fingerprint";
  kind_[SzlType::FLOAT] = "float";
  kind_[SzlType::INT] = "int";
  kind_[SzlType::STRING] = "string";
  kind_[SzlType::TIME] = "time";
  kind_[SzlType::TUPLE] = "tuple";
  kind_[SzlType::ARRAY] = "array";
  kind_[SzlType::MAP] = "map";

  base_[SzlType::BOOL] = true;
  base_[SzlType::BYTES] = true;
  base_[SzlType::FINGERPRINT] = true;
  base_[SzlType::FLOAT] = true;
  base_[SzlType::INT] = true;
  base_[SzlType::STRING] = true;
  base_[SzlType::TIME] = true;
}


SzlType::SzlType(Kind kind)
  : kind_(kind),
    param_(0),
    valid_(false),
    fields_(new vector<SzlField>),
    indices_(new vector<SzlField>),
    element_(NULL),
    weight_(NULL) {
}


SzlType::SzlType(const SzlType &type)
  : kind_(type.kind_),
    table_(type.table_),
    param_(type.param_),
    valid_(type.valid_),
    fields_(new vector<SzlField>(*type.fields_)),
    indices_(new vector<SzlField>(*type.indices_)),
    element_(NULL),
    weight_(NULL) {
  if (type.element_ != NULL)
    element_ = new SzlField(*type.element_);
  if (type.weight_ != NULL)
    weight_ = new SzlField(*type.weight_);
}

SzlType& SzlType::operator=(const SzlType& type) {
  // avoid self assignment
  if (this != &type) {
    kind_ = type.kind_;
    table_ = type.table_;
    param_ = type.param_;
    valid_ = type.valid_;
    *fields_ = *type.fields_;
    *indices_ = *type.indices_;
    set_element(type.element_);
    set_weight(type.weight_);
  }

  return *this;
}

SzlType::~SzlType() {
  delete fields_;
  delete indices_;
  delete element_;
  delete weight_;
}

string SzlType::KindName(Kind kind) {
  if (kind < 0 || kind > NKIND)
    return "<unknown type>";
  return tbl.kind_[kind];
}

void SzlType::set_kind(Kind kind) {
  kind_ = kind;
}

void SzlType::set_table(const string& table) {
  table_ = table;
}

void SzlType::set_param(int param) {
  param_ = param;
}

int SzlType::indices_size() const {
  return indices_->size();
}

void SzlType::indices_resize(int size) {
  indices_->resize(size);
}

const SzlField& SzlType::index(int i) const {
  return indices_->at(i);
}

void SzlType::AddIndex(const string &label, const SzlType &type) {
  indices_->push_back(SzlField(label, type));
  valid_ = false;
}

int SzlType::fields_size() const {
  return fields_->size();
}

void SzlType::fields_resize(int size) {
  fields_->resize(size);
}

const SzlField& SzlType::field(int i) const {
  return fields_->at(i);
}

void SzlType::AddField(const string &label, const SzlType &type) {
  fields_->push_back(SzlField(label, type));
  valid_ = false;
}

void SzlType::set_element(const SzlField* element) {
  delete element_;
  element_ = NULL;
  if (element != NULL)
    element_ = new SzlField(*element);
  valid_ = false;
}

void SzlType::set_element(const string &label, const SzlType &type) {
  delete element_;
  element_ = new SzlField(label, type);
  valid_ = false;
}

void SzlType::set_weight(const SzlField* weight) {
  delete weight_;
  weight_ = NULL;
  if (weight != NULL)
    weight_ = new SzlField(*weight);
  valid_ = false;
}

void SzlType::set_weight(const string &label, const SzlType &type) {
  delete weight_;
  weight_ = new SzlField(label, type);
  valid_ = false;
}

bool SzlType::BaseKind(Kind kind) {
  return tbl.base_[kind];
}

bool SzlType::BaseType() const {
  string error;
  if (!Valid(&error))
    return false;
  return tbl.base_[kind_];
}

bool SzlType::TupleType() const {
  string error;
  if (!Valid(&error))
    return false;
  return kind_ == TUPLE;
}

bool SzlType::TableType() const {
  string error;
  if (!Valid(&error))
    return false;
  return kind_ == TABLE;
}

bool SzlType::ValidIndices(string* error) const {
  for (int i = 0; i < indices_size(); ++i) {
    if (!index(i).type().Valid(error)) {
      return false;
    }
    if (index(i).type().kind_ == TABLE) {
      *error = "cannot have a table as another table's index";
      return false;
    }
  }
  return true;
}

bool SzlType::Valid(string* error) const {
  if (valid_)
    return true;

  switch (kind_) {
  default:
    if (!tbl.base_[kind_]) {
      *error = "unknown type kind";
      return false;
    }
    if (param_ != 0 || fields_size() != 0
    || indices_size() != 0
    || element_ != NULL || weight_ != NULL) {
      *error = "base type with some structural elements set";
      return false;
    }
    break;

  case TABLE: {
      if (table_.empty()) {
        *error = "no table type name";
        return false;
      }

      // Make sure this looks like a table type:
      // it must have valid indices, an element type, and no fields.
      // If there is a weight, it must also be valid.
      if (element_ == NULL) {
         *error = "no element type";
         return false;
      }
      if (!element_->type().Valid(error)) {
         return false;
      }
      if (element_->type().kind_ == TABLE) {
        *error = "can't have a table of tables";
        return false;
      }
      if (fields_size() != 0) {
        *error = "table can't have fields";
        return false;
      }
      if (!ValidIndices(error)) {
        return false;
      }

      if (weight_ != NULL) {
        if (!weight_->type().Valid(error)) {
          return false;
        }

        if (weight_->type().kind_ == TABLE) {
          *error = "can't have a table weighted by tables";
          return false;
        }
      }

      // Check to see if we have a weight iff it's needed.
      TableProperties props;
      if (!SzlResults::Properties(table_.c_str(), &props)) {
        *error = "unknown table type " + table_;
        return false;
      }
      if ((weight_ != NULL) != props.has_weight) {
        if (weight_ != NULL) {
          *error = "table has spurious weight";
        } else {
          *error = "table missing weight";
        }
        return false;
      }

      // Now ask the table implementation if this looks ok.
      if (!SzlResults::IsValid(*this, error)) {
        return false;
      }
    }
    break;

  case VOID:
    *error = "invalid type";
    return false;

  case TUPLE:
    if (param_ != 0) {
      *error = "tuples can't have params";
      return false;
    }
    if (indices_size() != 0) {
      *error = "tuples can't have indices";
      return false;
    }
    if (element_ != NULL) {
      *error = "tuples can't have an element type";
      return false;
    }
    if (weight_ != NULL) {
      *error = "tuples can't have weights";
      return false;
    }
    for (int i = 0; i < fields_size(); ++i) {
      if (field(i).type().kind_ == TABLE) {
        *error = "can't have a tuple with a table field";
        return false;
      }
      if (!field(i).type().Valid(error)) {
        return false;
      }
    }
    break;

  case ARRAY:
    if (param_ != 0) {
      *error = "arrays can't have params";
      return false;
    }
    if (indices_size() != 0) {
      *error = "arrays can't have indices";
      return false;
    }
    if (fields_size() != 0) {
      *error = "arrays can't have fields";
      return false;
    }
    if (weight_ != NULL) {
      *error = "arrays can't have weights";
      return false;
    }
    if (element_ == NULL) {
      *error = "arrays must have an element type";
      return false;
    }
    if (!element_->type().Valid(error)) {
      return false;
    }
    if (element_->type().kind_ == TABLE) {
      *error = "can't have an array of tables";
      return false;
    }
    break;

  case MAP:
    if (param_ != 0) {
      *error = "maps can't have params";
      return false;
    }
    if (weight_ != NULL) {
      *error = "maps can't have weights";
      return false;
    }
    if (fields_size() != 0) {
      *error = "maps can't have fields";
      return false;
    }
    if (indices_size() != 1) {
      *error = "maps must have exactly 1 index";
      return false;
    }
    if (!ValidIndices(error)) {
      return false;
    }
    if (element_ == NULL) {
      *error = "maps must have an element type";
      return false;
    }
    if (!element_->type().Valid(error)) {
      return false;
    }
    if (element_->type().kind_ == TABLE) {
      *error = "can't have a map of tables";
      return false;
    }
    break;
  }

  valid_ = true;
  return true;
}

string SzlFieldPPrint(const SzlField& elem) {
  string str;
  if (elem.label().size() > 0)
    str += elem.label() + ": ";
  str += elem.type().PPrint();
  return str;
}

string SzlType::PPrint() const {
  string error;
  if (!Valid(&error))
    return string("badtype<") + error + ">";

  string str;
  if (tbl.base_[kind_]) {
      return tbl.kind_[kind_];
  } else if (kind_ == TUPLE) {
    str = "{";
    for (int i = 0; i < fields_size(); ++i) {
     if (i > 0)
       str += ", ";
     str += SzlFieldPPrint(field(i));
    }
    str += "}";
  } else if (kind_ == ARRAY) {
    str = "array of ";
    str += SzlFieldPPrint(*element_);
  } else if (kind_ == MAP) {
    str = "map";
    str += "[" + SzlFieldPPrint(index(0)) + "]";
    str += " of " + SzlFieldPPrint(*element_);
  } else if (kind_ == TABLE) {
    TableProperties props;
    if (!SzlResults::Properties(table_.c_str(), &props))
      LOG(FATAL) << "can't get properties for a valid table type";
    str += "table ";
    str += props.name;
    if (props.has_param) {
      char buffer[20];
      snprintf(buffer, sizeof(buffer), "(%d)", param_);
      str += buffer;
    }
    for (int i = 0; i < indices_size(); ++i)
      str += "[" + SzlFieldPPrint(index(i)) + "]";
    str += " of " + SzlFieldPPrint(*element_);
    if (weight_ != NULL)
      str += " weight " + SzlFieldPPrint(*weight_);
  } else {
    str = string("can't PPrint ") + tbl.kind_[kind_] + " types";
  }

  return str;
}

ostream &operator <<(ostream& os, const SzlType& type) {
  return os << type.PPrint();
}

void SzlType::PartialReset() {
  delete weight_;
  weight_ = NULL;
  delete element_;
  element_ = NULL;
  string empty;
  table_.swap(empty);

  valid_ = false;
}

bool SzlField::InitFromSzlProto(const SzlFieldProto &fpb, string* error) {
  type_.PartialReset();  // reset state and clean up any allocated storage
  label_ = fpb.label();

  SzlTypeProto tpb;
  if (!tpb.ParseFromArray(fpb.type().c_str(), fpb.type().size())) {
    *error = kBadTypeParse;
    return false;
  }
  return type_.InitFromSzlProto(tpb, error);
}


bool SzlType::ParseFromSzlArray(const char * buf, int len, string* error) {
  PartialReset();   // reset state and clean up any allocated storage
  SzlTypeProto tpb;
  if (!tpb.ParseFromArray(buf, len)) {
    *error = kBadTypeParse;
    return false;
  }
  return InitFromSzlProto(tpb, error);
}


bool SzlType::InitFromSzlProto(const sawzall::SzlTypeProto& tpb,
                               string* error) {
  // map from protocol buffer kind & table to our kind.
  int protokind = tpb.kind();
  if (protokind == SzlTypeProto::TABLE) {
    if (!tpb.has_table() || tpb.table().empty()) {
      *error = kBadTypeParse;
      return false;
    }

    // All table types we know about are registered with SzlResults.
    if (!SzlResults::Properties(tpb.table().c_str(), NULL)) {
      *error = kUnknownKind;
      return false;
    }
    kind_ = TABLE;
    table_ = tpb.table();
  } else if (protokind < SzlTypeProto::NKIND && !tbl.kind_[protokind].empty()) {
    kind_ = static_cast<Kind>(protokind);
  } else {
    *error = kUnknownKind;
    return false;
  }

  param_ = tpb.param();
  indices_resize(tpb.indices_size());
  for (int i = 0 ; i < tpb.indices_size(); ++i) {
    if (!(*indices_)[i].InitFromSzlProto(tpb.indices(i), error))
      return false;
  }
  fields_resize(tpb.fields_size());
  for (int i = 0 ; i < tpb.fields_size(); ++i) {
    if (!(*fields_)[i].InitFromSzlProto(tpb.fields(i), error))
       return false;
  }
  if (tpb.has_element()) {
    element_ = new SzlField;
    if (!element_->InitFromSzlProto(tpb.element(), error))
      return false;
  }
  if (tpb.has_weight()) {
    weight_ = new SzlField;
    if (!weight_->InitFromSzlProto(tpb.weight(), error))
      return false;
  }

  return Valid(error);
}

bool SzlType::Equal(const SzlType& other) const {
  if (other.kind_ != kind_)
    return false;
  if (kind_ == SzlType::TABLE) {
    // Dig deeper
    if (other.table_ != table_)
      return false;
    if (!element_->type().Equal(other.element_->type()))
      return false;
    if (indices_size() != other.indices_size())
      return false;
    for (int i = 0; i < indices_size(); ++i) {
      if (!index(i).type().Equal(other.index(i).type()))
        return false;
    }
    if ((weight_ != NULL || other.weight_ != NULL)
        && (weight_ == NULL || other.weight_ == NULL
            || !weight_->type().Equal(other.weight_->type()))) {
      return false;
    }
  }
  // TODO: more to do here
  return true;
}
