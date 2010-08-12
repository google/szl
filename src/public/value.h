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

// This file is a top-level header file for the Sawzall component. No
// other lower-level Sawzall header file should be included by
// clients of the Sawzall implementation.

#ifndef _PUBLIC_VALUE_H_
#define _PUBLIC_VALUE_H_

#include <string>
#include <utility>  // For pair<>
#include <vector>


namespace sawzall {

// Forward declarations for classes declared here.
class Value;
class BoolValue;
class IntValue;
class UIntValue;
class FloatValue;
class FingerprintValue;
class TimeValue;
class BytesValue;
class StringValue;
class ArrayValue;
class TupleValue;
class MapValue;
class FunctionValue;

class ValueType;
class VoidValueType;
class BoolValueType;
class IntValueType;
class UIntValueType;
class FloatValueType;
class FingerprintValueType;
class TimeValueType;
class BytesValueType;
class StringValueType;
class ArrayValueType;
class TupleValueType;
class MapValueType;
class FunctionValueType;

class FieldType;

// Declarations for private classes, which C++ forces us to include here.
class Field;
class Proc;
class Type;
class Val;
class VarDecl;


// A FunctionDecl represents a reference to a global (static or
// non-static) function.  It is returned by Process::LookupFunction(),
// and used by Process::DoCall().
class FunctionDecl {
 public:
  const FunctionValueType* type() const;

 private:
  // Converts between the internal VarDecl* representation and the
  // public FunctionDecl* interface.
  static const FunctionDecl* New(const VarDecl* fun_decl);
  const VarDecl* fun_decl() const;

  // FunctionDecl is implemented internally simply as a casted
  // VarDecl, so there are no member variables declared here.

  friend class Process;
};


// Instances of CallContext are created by Process::SetupCall(), used
// when allocating Values (e.g., call arguments and results), passed
// to Process::DoCall(), and consumed by Process::FinishCall().  It
// helps users follow the proper protocol for the relative order of
// SetupCall(), Value allocation, DoCall(), result Value use, and
// FinishCall().  It also manages all the memory allocation and
// clean-up necessary.
class CallContext {
 private:
  // Constructs a CallContext, which stores a handle to the given Proc*.
  explicit CallContext(Proc* proc);

  // Destructs this CallContext, which has the side-effect of
  // decrementing the reference counts of Val*s registered with it.
  ~CallContext();

  Proc* proc();

  // Registers the allocation of the given Val*, to be
  // de-reference-counted when the call context is deallocated.
  void Record(Val* val);

  Proc* proc_;
  vector<Val*> vals_;

  friend class Process;
  friend class BoolValue;
  friend class IntValue;
  friend class UIntValue;
  friend class FloatValue;
  friend class FingerprintValue;
  friend class TimeValue;
  friend class BytesValue;
  friend class StringValue;
  friend class ArrayValue;
  friend class TupleValue;
  friend class MapValue;
  friend class FunctionValue;
};


// The Value class hierarchy provides a public interface for
// constructing and deconstructing Sawzall values.
//
// Note that there is no 'null' value in Sawzall.  A NULL Value*
// does not represent any legal Sawzall value.
//
// While Sawzall has a notion of 'undefined', there is not
// UndefinedValue, since it is not possoble to manipulate an
// undefined value in Sawzall, e.g., pass it in as an argument or
// return it as a result.
class Value {
 public:
  // Returns the type of this value.
  const ValueType* type() const;

  // Returns whether this Value* is of the given kind.
  bool is_bool() const;
  bool is_int() const;
  bool is_uint() const;
  bool is_float() const;
  bool is_fingerprint() const;
  bool is_time() const;
  bool is_bytes() const;
  bool is_string() const;
  bool is_array() const;
  bool is_tuple() const;
  bool is_map() const;
  bool is_function() const;

  // Returns whether this value is structurally equivalent to that value.
  bool IsEqual(const Value* that) const;

  // Narrowings.
  // Each requires that the corresponding is_X() operation returns true.
  const BoolValue* as_bool() const;
  const IntValue* as_int() const;
  const UIntValue* as_uint() const;
  const FloatValue* as_float() const;
  const FingerprintValue* as_fingerprint() const;
  const TimeValue* as_time() const;
  const BytesValue* as_bytes() const;
  const StringValue* as_string() const;
  const ArrayValue* as_array() const;
  const TupleValue* as_tuple() const;
  const MapValue* as_map() const;
  const FunctionValue* as_function() const;

 protected:
  // Converts between the internal Val* representation and the public
  // Value* interface.  The NewArray() and val_array() variants
  // convert between an array of Val*s and an array of Value*s.
  static const Value* New(const Val* val);
  static const Value* const* NewArray(const Val* const* vals);
  // These operations drop the 'const' prefix on the result Val* (s),
  // so that we can more easily invoke Val operations that don't
  // happen to say 'const' on them.  We never expose this unprotected
  // Val* to public clients.
  Val* val() const;
  static Val** val_array(const Value* const* values);

  // Value is implemented internally simply as a casted
  // Val, so there are no member variables declared here.

  friend class Process;
};


class BoolValue : public Value {
 public:
  // Creates a new Sawzall bool value corresponding to the given C++
  // bool value.
  static const BoolValue* New(CallContext* context, bool value);
  bool value() const;
};


class IntValue : public Value {
 public:
  // Creates a new Sawzall int value corresponding to the given C++
  // int64 value.
  static const IntValue* New(CallContext* context, int64 value);
  int64 value() const;
};


class UIntValue : public Value {
 public:
  // Creates a new Sawzall uint value corresponding to the given C++
  // uint64 value.
  static const UIntValue* New(CallContext* context, uint64 value);
  uint64 value() const;
};


class FloatValue : public Value {
 public:
  // Creates a new Sawzall float value corresponding to the given C++
  // double value.
  static const FloatValue* New(CallContext* context, double value);
  double value() const;
};


class FingerprintValue : public Value {
 public:
  // Creates a new Sawzall fingerprint value corresponding to the given C++
  // uint64 value.
  static const FingerprintValue* New(CallContext* context, uint64 value);
  uint64 value() const;
};


class TimeValue : public Value {
 public:
  // Creates a new Sawzall time value corresponding to the given C++
  // uint64 value.
  static const TimeValue* New(CallContext* context, uint64 value);
  uint64 value() const;
};


class BytesValue : public Value {
 public:
  // Creates a new Sawzall bytes value with the given length and bytes.
  static const BytesValue* New(CallContext* context,
                               int length,
                               const char* chars);

  // Returns the number of bytes in this value.
  int length() const;

  // Returns a pointer to the first byte in this value.
  // Both signed and unsigned interpretations are supported.
  const unsigned char* bytes() const;
  const char* chars() const;
};


// A StringValue holds a Unicode string encoded in UTF-8.
class StringValue : public Value {
 public:
  // Creates a new Sawzall string value with the given contents.
  // Allows embedded null characters.
  static const StringValue* New(CallContext* context,
                                const string& value);
  // Assumes chars is null-terminated.
  static const StringValue* New(CallContext* context,
                                const char* chars);
  // Allows embedded null characters.
  static const StringValue* New(CallContext* context,
                                int length,
                                const char* chars);

  // Returns the number of bytes in the UTF-8 encoding of this value
  // (not the number of unicode characters!)
  int length() const;

  // Returns a pointer to the first byte in the the UTF-8-encoded
  // bytes representing this string.
  const char* chars() const;
};


// An ArrayValue holds a dynamically determined number of elements,
// all of which have the same type.
class ArrayValue : public Value {
 public:
  // TODO: Implement this operation.
  // Creates a new Sawzall array value with the given properties.
  // static const ArrayValue* New(CallContext* context,
  //                              const ValueType* element_type,
  //                              int length,
  //                              const Value* const* elements);

  // Returns the number of elements in this array.
  int length() const;

  // Returns a pointer to the first element Value* in this array.
  const Value* const* elements() const;

  // Returns the i'th element Value* (origin 0) in this array; does
  // bounds checking.
  const Value* at(int i) const;
};


// A TupleValue holds a fixed number of elements, which can be of
// different types.
class TupleValue : public Value {
 public:
  // TODO: Implement this operation.
  // Creates a new Sawzall tuple value with the given properties.
  // static const TupleValue* New(
  //     CallContext* context,
  //     int length,
  //     const pair<const Value*, const ValueType*>* elements);

  // Returns the number of elements in this tuple.
  int length() const;

  // Returns a pointer to the first element Value* in this tuple.
  const Value* const* elements() const;

  // Returns the i'th element Value* (origin 0) in this tuple; does
  // bounds checking.
  const Value* at(int i) const;
};


class MapValue : public Value {
 public:
  // TODO: Flesh this out.
};


// Functions (which are called Closures in the Sawzall internals,
// since they include information about their lexically enclosing
// scope)
class FunctionValue : public Value {
 public:
  // TODO: Flesh this out.
};


// The ValueType class hierarchy provides a public interface for
// constructing and deconstructing Sawzall types of values.
class ValueType {
 public:
  // Returns the top-level kind of this type.
  enum Kind {VOID, BOOL, INT, UINT, FLOAT, FINGERPRINT, TIME,
             BYTES, STRING, ARRAY, TUPLE, MAP, FUNCTION};
  Kind kind() const;

  // Returns whether this ValueType* is of the given kind.
  bool is_void() const;
  bool is_bool() const;
  bool is_int() const;
  bool is_uint() const;
  bool is_float() const;
  bool is_fingerprint() const;
  bool is_time() const;
  bool is_bytes() const;
  bool is_string() const;
  bool is_array() const;
  bool is_tuple() const;
  bool is_map() const;
  bool is_function() const;

  // Returns whether this type is structurally equivalent to that type.
  bool IsEqual(const ValueType* that) const;

  // Narrowings.
  // Each requires that the corresponding is_X() operation returns true.
  const VoidValueType* as_void() const;
  const BoolValueType* as_bool() const;
  const IntValueType* as_int() const;
  const UIntValueType* as_uint() const;
  const FloatValueType* as_float() const;
  const FingerprintValueType* as_fingerprint() const;
  const TimeValueType* as_time() const;
  const BytesValueType* as_bytes() const;
  const StringValueType* as_string() const;
  const ArrayValueType* as_array() const;
  const TupleValueType* as_tuple() const;
  const MapValueType* as_map() const;
  const FunctionValueType* as_function() const;

 protected:
  // Converts between the internal Type* representation and the public
  // ValueType* interface.
  static const ValueType* New(const Type* type);
  Type* type() const;

  // ValueType is implemented internally simply as a casted
  // Type, so there are no member variables declared here.

  friend class FunctionDecl;
  friend class Process;
  friend class Value;
  friend class FieldType;
};


class VoidValueType : public ValueType {
 public:
  static const VoidValueType* New();
};


class BoolValueType : public ValueType {
 public:
  static const BoolValueType* New();
};


class IntValueType : public ValueType {
 public:
  static const IntValueType* New();
};


class UIntValueType : public ValueType {
 public:
  static const UIntValueType* New();
};


class FloatValueType : public ValueType {
 public:
  static const FloatValueType* New();
};


class FingerprintValueType : public ValueType {
 public:
  static const FingerprintValueType* New();
};


class TimeValueType : public ValueType {
 public:
  static const TimeValueType* New();
};


class BytesValueType : public ValueType {
 public:
  static const BytesValueType* New();
};


class StringValueType : public ValueType {
 public:
  static const StringValueType* New();
};


class ArrayValueType : public ValueType {
 public:
  // TODO: Implement this operation.
  // static const ArrayValueType* New(const FieldType* element_type);

  // Returns the type of the array elements.
  const FieldType* element_type() const;
};


class TupleValueType : public ValueType {
 public:
  // TODO: Implement this operation.
  // static const TupleValueType* New(int length,
  //                                  const FieldType* const* element_types);

  // Returns the number of elements in this tuple type.
  int length() const;

  // Returns a pointer to the first element FieldType* in this tuple type.
  const FieldType* const* element_types() const;

  // Returns the i'th element FieldType* (origin 0) in this tuple
  // type; does bounds checking.
  const FieldType* at(int i) const;
};


class MapValueType : public ValueType {
 public:
  // TODO: Implement this operation.
  // static const MapValueType* New(const FieldType* key_type,
  //                                const FieldType* value_type);

  // Returns the type of the keys of this map type.
  const FieldType* key_type() const;

  // Returns the type of the values of this map type.
  const FieldType* value_type() const;
};


class FunctionValueType : public ValueType {
 public:
  // TODO: Implement this operation.
  // static const FunctionValueType* New(int num_args,
  //                                     const FieldType* const* arg_types,
  //                                     const FieldType* result_type);

  // Returns the number of arguments of this function type.
  int num_args() const;

  // Returns a pointer to the first argument FieldType* in this function type.
  const FieldType* const* arg_types() const;

  // Returns the result FieldType* of this function type.  The result
  // FieldType's type() will be VoidValueType if this function type
  // has no result.
  const FieldType* result_type() const;
};


// FieldType is the representation of a Sawzall type that may optionally
// have a name.
class FieldType {
 public:
  // Creates an anonymous field type.
  // TODO: Implement this operation.
  // static const FieldType* New(const ValueType* type);

  // Creates a named field type.
  // TODO: Implement this operation.
  // static const FieldType* New(const char* name, const ValueType* type);

  // Returns the name of this field type, or NULL if anonymous.
  const char* name() const;

  // Returns the type of this field type.
  const ValueType* type() const;

 private:
  // Converts between the internal Field* representation and the public
  // FieldType* interface.  The NewArray() and field_array() variants
  // convert between an array of Field*s and an array of FieldType*s.
  static const FieldType* New(const Field* field);
  static const FieldType* const* NewArray(const Field* const* fields);
  // These operations drop the 'const' prefix on the result Field* (s),
  // so that we can more easily invoke Field operations that don't
  // happen to say 'const' on them.  We never expose this unprotected
  // Field* to public clients.
  Field* field() const;
  static Field** field_array(const FieldType* const* value_type_fields);

  // FieldType is implemented internally simply as a casted
  // Field, so there are no member variables declared here.

  friend class ArrayValueType;
  friend class TupleValueType;
  friend class MapValueType;
  friend class FunctionValueType;
};


}  // namespace sawzall

#endif  // _PUBLIC_VALUE_H_
