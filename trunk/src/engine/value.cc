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

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/code.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/frame.h"
#include "engine/tracer.h"
#include "engine/codegen.h"
#include "engine/compiler.h"
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "public/value.h"
#include "public/sawzall.h"

namespace sawzall {


// ----------------------------------------------------------------------------
// FunctionDecl

// A FunctionDecl* is just a casted VarDecl*.

const FunctionDecl* FunctionDecl::New(const VarDecl* fun_decl) {
  return reinterpret_cast<const FunctionDecl*>(fun_decl);
}

const VarDecl* FunctionDecl::fun_decl() const {
  return reinterpret_cast<const VarDecl*>(this);
}

const FunctionValueType* FunctionDecl::type() const {
  return ValueType::New(fun_decl()->type())->as_function();
}


// ----------------------------------------------------------------------------
// CallContext

CallContext::CallContext(Proc* proc) : proc_(proc) {
}

CallContext::~CallContext() {
  int num_vals = vals_.size();
  for (int i = 0; i < num_vals; i++) {
    Val* val = vals_.at(i);
    val->dec_ref();
  }
  vals_.clear();
  proc_ = NULL;
}

Proc* CallContext::proc() {
  CHECK(proc_ != NULL) << "cannot allocate values after FinishCall()";
  return proc_;
}

void CallContext::Record(Val* val) {
  vals_.push_back(val);
}


// ----------------------------------------------------------------------------
// Value

// A Value* is just a casted Val*.

const Value* Value::New(const Val* val) {
  return reinterpret_cast<const Value*>(val);
}

const Value* const* Value::NewArray(const Val* const* vals) {
  return reinterpret_cast<const Value* const*>(vals);
}

Val* Value::val() const {
  return const_cast<Val*>(reinterpret_cast<const Val*>(this));
}

Val** Value::val_array(const Value* const* values) {
  return const_cast<Val**>(reinterpret_cast<const Val* const*>(values));
}

const ValueType* Value::type() const {
  return ValueType::New(val()->type());
}

bool Value::is_bool() const { return val()->is_bool(); }
bool Value::is_int() const { return val()->is_int(); }
bool Value::is_uint() const { return val()->is_uint(); }
bool Value::is_float() const { return val()->is_float(); }
bool Value::is_fingerprint() const { return val()->is_fingerprint(); }
bool Value::is_time() const { return val()->is_time(); }
bool Value::is_bytes() const { return val()->is_bytes(); }
bool Value::is_string() const { return val()->is_string(); }
bool Value::is_array() const { return val()->is_array(); }
bool Value::is_tuple() const { return val()->is_tuple(); }
bool Value::is_map() const { return val()->is_map(); }
bool Value::is_function() const { return val()->is_closure(); }

bool Value::IsEqual(const Value* that) const {
  return this->val()->IsEqual(that->val());
}

const BoolValue* Value::as_bool() const {
  assert(is_bool());
  return reinterpret_cast<const BoolValue*>(this);
}

const IntValue* Value::as_int() const {
  assert(is_int());
  return reinterpret_cast<const IntValue*>(this);
}

const UIntValue* Value::as_uint() const {
  assert(is_uint());
  return reinterpret_cast<const UIntValue*>(this);
}

const FloatValue* Value::as_float() const {
  assert(is_float());
  return reinterpret_cast<const FloatValue*>(this);
}

const FingerprintValue* Value::as_fingerprint() const {
  assert(is_fingerprint());
  return reinterpret_cast<const FingerprintValue*>(this);
}

const TimeValue* Value::as_time() const {
  assert(is_time());
  return reinterpret_cast<const TimeValue*>(this);
}

const BytesValue* Value::as_bytes() const {
  assert(is_bytes());
  return reinterpret_cast<const BytesValue*>(this);
}

const StringValue* Value::as_string() const {
  assert(is_string());
  return reinterpret_cast<const StringValue*>(this);
}

const ArrayValue* Value::as_array() const {
  assert(is_array());
  return reinterpret_cast<const ArrayValue*>(this);
}

const TupleValue* Value::as_tuple() const {
  assert(is_tuple());
  return reinterpret_cast<const TupleValue*>(this);
}

const MapValue* Value::as_map() const {
  assert(is_map());
  return reinterpret_cast<const MapValue*>(this);
}

const FunctionValue* Value::as_function() const {
  assert(is_function());
  return reinterpret_cast<const FunctionValue*>(this);
}


// ----------------------------------------------------------------------------
// BoolValue

const BoolValue* BoolValue::New(CallContext* context, bool value) {
  Val* val = Factory::NewBool(context->proc(), value);
  context->Record(val);
  return Value::New(val)->as_bool();
}

bool BoolValue::value() const {
  return val()->as_bool()->val();
}


// ----------------------------------------------------------------------------
// IntValue

const IntValue* IntValue::New(CallContext* context, int64 value) {
  Val* val = Factory::NewInt(context->proc(), value);
  context->Record(val);
  return Value::New(val)->as_int();
}

int64 IntValue::value() const {
  return val()->as_int()->val();
}


// ----------------------------------------------------------------------------
// UIntValue

const UIntValue* UIntValue::New(CallContext* context, uint64 value) {
  Val* val = Factory::NewUInt(context->proc(), value);
  context->Record(val);
  return Value::New(val)->as_uint();
}

uint64 UIntValue::value() const {
  return val()->as_uint()->val();
}


// ----------------------------------------------------------------------------
// FloatValue

const FloatValue* FloatValue::New(CallContext* context, double value) {
  Val* val = Factory::NewFloat(context->proc(), value);
  context->Record(val);
  return Value::New(val)->as_float();
}

double FloatValue::value() const {
  return val()->as_float()->val();
}


// ----------------------------------------------------------------------------
// FingerprintValue

const FingerprintValue* FingerprintValue::New(CallContext* context,
                                              uint64 value) {
  Val* val = Factory::NewFingerprint(context->proc(), value);
  context->Record(val);
  return Value::New(val)->as_fingerprint();
}

uint64 FingerprintValue::value() const {
  return val()->as_fingerprint()->val();
}


// ----------------------------------------------------------------------------
// TimeValue

const TimeValue* TimeValue::New(CallContext* context, uint64 value) {
  Val* val = Factory::NewTime(context->proc(), value);
  context->Record(val);
  return Value::New(val)->as_time();
}

uint64 TimeValue::value() const {
  return val()->as_time()->val();
}


// ----------------------------------------------------------------------------
// BytesValue

const BytesValue* BytesValue::New(CallContext* context,
                                  int length,
                                  const char* chars) {
  Val* val = Factory::NewBytesInit(context->proc(), length, chars);
  context->Record(val);
  return Value::New(val)->as_bytes();
}

int BytesValue::length() const {
  return val()->as_bytes()->length();
}

const unsigned char* BytesValue::bytes() const {
  return val()->as_bytes()->u_base();
}

const char* BytesValue::chars() const {
  return val()->as_bytes()->base();
}


// ----------------------------------------------------------------------------
// StringValue

const StringValue* StringValue::New(CallContext* context,
                                    const string& value) {
  Val* val = Factory::NewStringCPP(context->proc(), value);
  context->Record(val);
  return Value::New(val)->as_string();
}

const StringValue* StringValue::New(CallContext* context,
                                    const char* chars) {
  Val* val = Factory::NewStringC(context->proc(), chars);
  context->Record(val);
  return Value::New(val)->as_string();
}

const StringValue* StringValue::New(CallContext* context,
                                    int length,
                                    const char* chars) {
  Val* val = Factory::NewStringBytes(context->proc(), length, chars);
  context->Record(val);
  return Value::New(val)->as_string();
}

int StringValue::length() const {
  return val()->as_string()->length();
}

const char* StringValue::chars() const {
  return val()->as_string()->base();
}


// ----------------------------------------------------------------------------
// ArrayValue

int ArrayValue::length() const {
  return val()->as_array()->length();
}

const Value* const* ArrayValue::elements() const {
  return NewArray(val()->as_array()->base());
}

const Value* ArrayValue::at(int i) const {
  CHECK(i >= 0 && i < length()) << "accessing array element out of bounds";
  return Value::New(val()->as_array()->at(i));
}


// ----------------------------------------------------------------------------
// TupleValue

int TupleValue::length() const {
  return val()->type()->as_tuple()->nslots();
}

const Value* const* TupleValue::elements() const {
  return NewArray(val()->as_tuple()->base());
}

const Value* TupleValue::at(int i) const {
  CHECK(i >= 0 && i < length()) << "accessing tuple element out of bounds";
  return Value::New(val()->as_tuple()->slot_at(i));
}


// ----------------------------------------------------------------------------
// ValueType

// A ValueType* is just a casted Type*.

const ValueType* ValueType::New(const Type* type) {
  return reinterpret_cast<const ValueType*>(type);
}

Type* ValueType::type() const {
  return const_cast<Type*>(reinterpret_cast<const Type*>(this));
}

ValueType::Kind ValueType::kind() const {
  switch (type()->fine_type()) {
    case Type::VOID:
      return ValueType::VOID;
    case Type::BOOL:
      return ValueType::BOOL;
    case Type::INT:
      return ValueType::INT;
    case Type::UINT:
      return ValueType::UINT;
    case Type::FLOAT:
      return ValueType::FLOAT;
    case Type::FINGERPRINT:
      return ValueType::FINGERPRINT;
    case Type::TIME:
      return ValueType::TIME;
    case Type::BYTES:
      return ValueType::BYTES;
    case Type::STRING:
      return ValueType::STRING;
    case Type::ARRAY:
      return ValueType::ARRAY;
    case Type::TUPLE:
      return ValueType::TUPLE;
    case Type::MAP:
      return ValueType::MAP;
    case Type::FUNCTION:
      return ValueType::FUNCTION;

    case Type::BOGUSF:
    case Type::BAD:
    case Type::INCOMPLETE:
    case Type::OUTPUT:
    default:
      LOG(FATAL) << "unexpected kind of type";
      return ValueType::VOID;
  }
}

bool ValueType::is_void() const { return type()->is_void(); }
bool ValueType::is_bool() const { return type()->is_bool(); }
bool ValueType::is_int() const { return type()->is_int(); }
bool ValueType::is_uint() const { return type()->is_uint(); }
bool ValueType::is_float() const { return type()->is_float(); }
bool ValueType::is_fingerprint() const { return type()->is_fingerprint(); }
bool ValueType::is_time() const { return type()->is_time(); }
bool ValueType::is_bytes() const { return type()->is_bytes(); }
bool ValueType::is_string() const { return type()->is_string(); }
bool ValueType::is_array() const { return type()->is_array(); }
bool ValueType::is_tuple() const { return type()->is_tuple(); }
bool ValueType::is_map() const { return type()->is_map(); }
bool ValueType::is_function() const { return type()->is_function(); }

bool ValueType::IsEqual(const ValueType* that) const {
  return this->type()->IsEqual(that->type(), false /* test_proto */);
}

const VoidValueType* ValueType::as_void() const {
  assert(is_void());
  return reinterpret_cast<const VoidValueType*>(this);
}

const BoolValueType* ValueType::as_bool() const {
  assert(is_bool());
  return reinterpret_cast<const BoolValueType*>(this);
}

const IntValueType* ValueType::as_int() const {
  assert(is_int());
  return reinterpret_cast<const IntValueType*>(this);
}

const UIntValueType* ValueType::as_uint() const {
  assert(is_uint());
  return reinterpret_cast<const UIntValueType*>(this);
}

const FloatValueType* ValueType::as_float() const {
  assert(is_float());
  return reinterpret_cast<const FloatValueType*>(this);
}

const FingerprintValueType* ValueType::as_fingerprint() const {
  assert(is_fingerprint());
  return reinterpret_cast<const FingerprintValueType*>(this);
}

const TimeValueType* ValueType::as_time() const {
  assert(is_time());
  return reinterpret_cast<const TimeValueType*>(this);
}

const BytesValueType* ValueType::as_bytes() const {
  assert(is_bytes());
  return reinterpret_cast<const BytesValueType*>(this);
}

const StringValueType* ValueType::as_string() const {
  assert(is_string());
  return reinterpret_cast<const StringValueType*>(this);
}

const ArrayValueType* ValueType::as_array() const {
  assert(is_array());
  return reinterpret_cast<const ArrayValueType*>(this);
}

const TupleValueType* ValueType::as_tuple() const {
  assert(is_tuple());
  return reinterpret_cast<const TupleValueType*>(this);
}

const MapValueType* ValueType::as_map() const {
  assert(is_map());
  return reinterpret_cast<const MapValueType*>(this);
}

const FunctionValueType* ValueType::as_function() const {
  assert(is_function());
  return reinterpret_cast<const FunctionValueType*>(this);
}


// ----------------------------------------------------------------------------
// Scalar ValueTypes

const VoidValueType* VoidValueType::New() {
  return ValueType::New(SymbolTable::void_type())->as_void();
}

const BoolValueType* BoolValueType::New() {
  return ValueType::New(SymbolTable::bool_type())->as_bool();
}

const IntValueType* IntValueType::New() {
  return ValueType::New(SymbolTable::int_type())->as_int();
}

const UIntValueType* UIntValueType::New() {
  return ValueType::New(SymbolTable::uint_type())->as_uint();
}

const FloatValueType* FloatValueType::New() {
  return ValueType::New(SymbolTable::float_type())->as_float();
}

const FingerprintValueType* FingerprintValueType::New() {
  return ValueType::New(SymbolTable::fingerprint_type())->as_fingerprint();
}

const TimeValueType* TimeValueType::New() {
  return ValueType::New(SymbolTable::time_type())->as_time();
}

const BytesValueType* BytesValueType::New() {
  return ValueType::New(SymbolTable::bytes_type())->as_bytes();
}

const StringValueType* StringValueType::New() {
  return ValueType::New(SymbolTable::string_type())->as_string();
}


// ----------------------------------------------------------------------------
// ArrayValueType

const FieldType* ArrayValueType::element_type() const {
  return FieldType::New(type()->as_array()->elem());
}


// ----------------------------------------------------------------------------
// TupleValueType

int TupleValueType::length() const {
  return type()->as_tuple()->nslots();
}

const FieldType* const* TupleValueType::element_types() const {
  Field** fields = type()->as_tuple()->fields()->data();
  return FieldType::NewArray(fields);
}

const FieldType* TupleValueType::at(int i) const {
  CHECK(i >= 0 && i < length());
  const Field* field = (*type()->as_tuple()->fields())[i];
  return FieldType::New(field);
}


// ----------------------------------------------------------------------------
// MapValueType

const FieldType* MapValueType::key_type() const {
  return FieldType::New(type()->as_map()->index());
}

const FieldType* MapValueType::value_type() const {
  return FieldType::New(type()->as_map()->elem());
}


// ----------------------------------------------------------------------------
// FunctionValueType

int FunctionValueType::num_args() const {
  return type()->as_function()->parameters()->length();
}

const FieldType* const* FunctionValueType::arg_types() const {
  Field** fields = type()->as_function()->parameters()->data();
  return FieldType::NewArray(fields);
}

const FieldType* FunctionValueType::result_type() const {
  return FieldType::New(type()->as_function()->result());
}


// ----------------------------------------------------------------------------
// FieldType

// A FieldType* is just a casted Field*.

const FieldType* FieldType::New(const Field* field) {
  return reinterpret_cast<const FieldType*>(field);
}

const FieldType* const* FieldType::NewArray(const Field* const* fields) {
  return reinterpret_cast<const FieldType* const*>(fields);
}

Field* FieldType::field() const {
  return const_cast<Field*>(reinterpret_cast<const Field*>(this));
}

Field** FieldType::field_array(const FieldType* const* value_type_fields) {
  return
      const_cast<Field**>(
          reinterpret_cast<const Field* const*>(value_type_fields));
}

const char* FieldType::name() const {
  return field()->name();
}

const ValueType* FieldType::type() const {
  return ValueType::New(field()->type());
}


}  // namespace sawzall
