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

// All the internal types used by the Sawzall system.


namespace sawzall {

class TypeVisitor;
class SzlTypeProto;
class FileLine;

// Types define the set of legal values and the structure of
// Sawzall objects such as literals, composites, variables, etc.
// Type is the abstract superclass of all types.
//
// Types are also carriers of value-specific functionality, such
// as printing, certain conversions, etc.
class Type {
 public:
  // Size in bytes
  virtual size_t size() const = 0;

  // My actual type, for is_foo testing.
  enum FineType {BOGUSF, BAD, INCOMPLETE, INT, UINT, BOOL, FLOAT,
              STRING, TIME, BYTES, FINGERPRINT, VOID, TUPLE,
              ARRAY, OUTPUT, FUNCTION, MAP};
  enum GrossType {BOGUSG, BASIC64, BASIC};

  // If a type has a type name, it can be referred to via an identifier
  // within a Sawzall program. Otherwise, it is an anonymous type.
  TypeName* type_name() const  { return type_name_; }
  void set_type_name(TypeName* type_name)  {
    assert(type_name_ == NULL);  // do assignment at most once!
    type_name_ = type_name;
  }

  // Conversions
  virtual ArrayType* as_array()  { return NULL; }
  virtual BadType* as_bad()  { return NULL; }
  virtual BasicType* as_basic()  { return NULL; }
  virtual FunctionType* as_function()  { return NULL; }
  virtual IncompleteType* as_incomplete()  { return NULL; }
  virtual MapType* as_map()  { return NULL; }
  virtual OutputType* as_output()  { return NULL; }
  virtual TupleType* as_tuple()  { return NULL; }

  // Type testers
  bool is_array() const  { return fine_type_ == ARRAY; }
  bool is_bad() const  { return fine_type_ == BAD; }
  bool is_basic() const  { return (gross_type_ == BASIC64 || gross_type_ == BASIC) && fine_type_ != FUNCTION; }
  bool is_function() const  { return fine_type_ == FUNCTION; }
  bool is_incomplete() const  { return fine_type_ == INCOMPLETE; }
  bool is_map() const  { return fine_type_ == MAP; }
  bool is_output() const  { return fine_type_ == OUTPUT; }
  bool is_tuple() const  { return fine_type_ == TUPLE; }

  // BasicType type testers
  bool is_bool() const  { return fine_type_ == BOOL; }
  bool is_bytes() const  { return fine_type_ == BYTES; }
  bool is_fingerprint() const  { return fine_type_ == FINGERPRINT; }
  bool is_float() const  { return fine_type_ == FLOAT; }
  bool is_int() const  { return fine_type_ == INT; }
  bool is_uint() const  { return fine_type_ == UINT; }
  bool is_string() const  { return fine_type_ == STRING; }
  bool is_time() const  { return fine_type_ == TIME; }
  bool is_void() const  { return fine_type_ == VOID; }
  bool is_basic64() const  { return gross_type_ == BASIC64; }  // a 64-bit scalar type

  // An unfinished type is a composite type that has been started but not
  // finished.  Any reference to an unfinished type is a recursive reference.
  virtual bool is_finished() const  { return true; }

  // Composite types are composed of 0 or more components.
  // All composite types are structured, but not all structured
  // types are composites (notably bytes and strings).
  bool is_composite() const {
    return fine_type_ == ARRAY || fine_type_ == MAP || fine_type_ == TUPLE;
  }

  // Structured types are non-atomic, i.e., they consist of smaller
  // parts that can be extracted. All composite types are structured,
  // but so are bytes and strings.
  bool is_structured() const {
    return fine_type_ == BYTES || fine_type_ == STRING || is_composite();
  }

  // Indexable types are structured types with integer index-addressable
  // components, called elements. Arrays, strings, and bytes are
  // indexable. Note that maps appear indexable in the syntax but are
  // not consider indexable for this function.
  bool is_indexable() const {
    return fine_type_ == STRING || fine_type_ == ARRAY || fine_type_ == BYTES;
  }

  // The elem_type() function generalizes the corresponding
  // function in ArrayType and also includes strings and bytes.
  virtual Type* elem_type() const  { return NULL; }

  // Allocatable types are structured types which can be allocated
  // using new(...). Indexable types are allocatable, and so are maps.
  bool is_allocatable() const {
    return fine_type_ == MAP || is_indexable();
  }

  // Proto types are types that can be used within a proto tuple.
  //
  // Make a proto type from the current type. The current
  // type is returned, if it is already a proto type, or if the
  // conversion fails (e.g. because the type contains a map).
  // Test with is_proto() to see if the conversion succeeded.
 protected:
  struct ProtoForward {
    Type* type;            // the original enclosing type
    Type* proto;           // the proto version of that type
    ProtoForward* parent;  // the next enclosing type
  };
 public:
  virtual Type* MakeProto(Proc* proc, ProtoForward* forward)  { return this; }
  static Field* MakeProtoField(Proc* proc, Field* f, ProtoForward* forward);
  virtual bool is_proto() const  { return false; }

  // Mark contained tuple types as tested for equality.
  virtual void set_tested_for_equality() { }

  // Turn on the read bits of all fields, including nested fields
  // when recurse is true.
  virtual void SetAllFieldsRead(bool recurse)  { }
  // Turn off the read bits of all fields, not recursive because types
  // are shared.
  virtual void ClearAllFieldsRead() { }

  // Type equality (symmetric)
  bool IsEqual(Type* t, bool test_proto);

  // TypeVisitor
  virtual void Visit(TypeVisitor* v) = 0;
  virtual void VisitChildren(TypeVisitor* v)  { }

  // Retrieve the type description fields
  FineType fine_type() const  { return fine_type_; }
  GrossType gross_type() const  { return gross_type_; }

  virtual ~Type() {}

  TupleType* enclosing_tuple() const  { return enclosing_tuple_; }

 protected:
  virtual bool IsEqualType(Type* t, bool test_proto) = 0;

 private:
  TypeName* type_name_;

 protected:
  FineType fine_type_;
  GrossType gross_type_;

  TupleType* enclosing_tuple_;  // tuple of enclosing scope, if any

  void Initialize();

  // Prevent construction from outside the class (must use factory method)
  Type() { /* nothing to do */ }
};


// A BadType is used as a "catch-all" for type errors: it is compatible
// with all other types for that purpose.
class BadType: public Type {
 public:
  // Creation
  static BadType* New(Proc* proc);

  // Type interface support
  virtual size_t size() const { return sizeof(Val*); }
  virtual BadType* as_bad()  { return this; }
  virtual void Visit(TypeVisitor* v);

  // Treat this as a proto type to minimize error messages.
  virtual bool is_proto() const  { return true; }

 private:
  virtual bool IsEqualType(Type* t, bool test_proto);

  // Prevent construction from outside the class (must use factory method)
  BadType() {}
};


// IncompleteType is the default type for all (yet to be properly typed)
// nodes. It is not compatible with any type (not even itself).
class IncompleteType: public Type {
 public:
  // Creation
  static IncompleteType* New(Proc* proc);

  // Type interface support
  virtual size_t size() const { return sizeof(Val*); }
  virtual IncompleteType* as_incomplete()  { return this; }
  virtual void Visit(TypeVisitor* v);

 private:
  virtual bool IsEqualType(Type* t, bool test_proto);

  // Prevent construction from outside the class (must use factory method)
  IncompleteType() {}
};


// BasicTypes are the elementary and predefined Sawzall types.
class BasicType: public Type {
 public:
  enum Kind {
    // external types (accessible in Sawzall programs)
    BOOL, BYTES, FINGERPRINT, FLOAT, INT, STRING, TIME, UINT,
    // internal types (not accessible in Sawzall programs)
    VOID
  };

  // The number of external types
  static const int NBasic = UINT + 1;  // outside enum to silence g++ -Wall

  // Creation
  static BasicType* New(Proc* proc, Kind kind);

  // Predicates
  virtual bool is_bool() const  { return kind_ == BOOL; }
  virtual bool is_bytes() const  { return kind_ == BYTES; }
  virtual bool is_fingerprint() const  { return kind_ == FINGERPRINT; }
  virtual bool is_float() const  { return kind_ == FLOAT; }
  virtual bool is_int() const { return kind_ == INT; }
  virtual bool is_uint() const { return kind_ == UINT; }
  virtual bool is_string() const { return kind_ == STRING; }
  virtual bool is_time() const { return kind_ == TIME; }
  virtual bool is_void() const { return kind_ == VOID; }
  virtual bool is_basic64() const {
    // Bools take 64 bits since they're stored in a slot except within arrays.
    // Moreover, since there is no auto-conversion of ints etc. to bool,
    // internally they will always have value 0LL or 1LL.  It is therefore
    // safe to treat them the same as ints for ops like fingerprinting.
    return is_bool() || is_int() || is_uint() ||
           is_fingerprint() || is_float() || is_time();
  }

  // Accessors
  Kind kind() const  { return kind_; }
  virtual Type* elem_type() const;
  virtual bool is_proto() const { return true; }

  // Type interface support
  virtual size_t size() const;
  virtual BasicType* as_basic()  { return this; }
  virtual void Visit(TypeVisitor* v);

  // Printing support
  static const char* Kind2String(Kind kind);

  // Runtime support
  Form* form() const {
    return form_;
  }

  BoolForm* bool_form() const {
    assert(is_bool());
    return reinterpret_cast<BoolForm*>(form_);
  }

  BytesForm* bytes_form() const {
    assert(is_bytes());
    return reinterpret_cast<BytesForm*>(form_);
  }

  FingerprintForm* fingerprint_form() const {
    assert(is_fingerprint());
    return reinterpret_cast<FingerprintForm*>(form_);
  }

  FloatForm* float_form() const {
    assert(is_float());
    return reinterpret_cast<FloatForm*>(form_);
  }

  IntForm* int_form() const {
    assert(is_int());
    return reinterpret_cast<IntForm*>(form_);
  }

  UIntForm* uint_form() const {
    assert(is_uint());
    return reinterpret_cast<UIntForm*>(form_);
  }

  StringForm* string_form() const {
    assert(is_string());
    return reinterpret_cast<StringForm*>(form_);
  }

  TimeForm* time_form() const {
    assert(is_time());
    return reinterpret_cast<TimeForm*>(form_);
  }

 private:
  Kind kind_;
  Form* form_;

  virtual bool IsEqualType(Type* t, bool test_proto);

  // Prevent construction from outside the class (must use factory method)
  BasicType() {}
};


// Enum for representing protocol buffer types
enum ProtoBufferType {
  PBTYPE_UNKNOWN,
  PBTYPE_DOUBLE,
  PBTYPE_FLOAT,
  PBTYPE_INT64,
  PBTYPE_UINT64,
  PBTYPE_INT32,
  PBTYPE_UINT32,
  PBTYPE_FIXED64,
  PBTYPE_FIXED32,
  PBTYPE_BOOL,
  PBTYPE_BYTES,
  PBTYPE_STRING,
};


// TupleTypes represent aggregates of named or unnamed fields of various types.
class TupleType: public Type {
 public:
  // Creation
  // Note: proto map creation may fail if the proto tags are too
  // far apart. Thus, when is_proto is set, after creation the caller
  // should check if is_proto() is true. If it is not, proto map creation
  // failed and an error should be issued.
  static TupleType* New(Proc* proc, Scope* scope, bool is_proto,
                        bool is_message, bool is_predefined);

  // Creation, split into two parts so we have a type to which to refer
  // for recursive references.
  static TupleType* NewUnfinished(Proc* proc, Scope* scope, TypeName* tname,
                                  TupleType* enclosing_tuple);
  TupleType* Finish(Proc* proc, bool is_proto, bool is_message,
                    bool is_predefined);

  // Bind fields to slots for all tuple types.
  // This must be done after all the code is parsed and before the number of
  // slots or field-to-slot mapping is required.  We defer binding (except for
  // predefined tuples) so that we can omit slots for unreferenced fields.
  static void BindFieldsToSlotsForAll(Proc* proc);

  // Make a proto tuple from the current tuple. The current
  // tuple is returned, if it is already a proto tuple, or if the
  // conversion fails (e.g. because the tuple contains a map).
  // Test with is_proto() to see if the conversion succeeded.
  virtual Type* MakeProto(Proc* proc, ProtoForward* forward);

  // Accessors
  virtual bool is_finished() const  { return is_finished_; }
  Scope* scope() const  { return scope_; }
  List<Field*>* fields() const  { return fields_; }
  // the number of tuple slots (may be less than the number of fields)
  int nslots() const  { assert(fields_bound()); return nslots_; }
  // the tuple length in slots, including space for inproto bits
  int ntotal() const  { assert(fields_bound()); return ntotal_; }
  List<int>* map() const  { return map_; }
  int min_tag() const  { return min_tag_; }
  int inproto_index(Field* f) const;  // the inproto bit index for field f
  virtual bool is_proto() const  { return map_ != NULL || is_empty(); }
  bool is_message() const  { return is_proto() && is_message_; }
  bool is_auto_proto() const  { return is_auto_proto_; }
  bool is_empty() const  { return fields_->is_empty(); }
  bool fields_bound() const  { return (nslots_ >= 0); }
  TupleVal* default_proto_val() const {
    // Note that the default is not allocated until the fields are bound.
    assert(is_proto());
    assert(default_proto_val_ != NULL);
    return default_proto_val_;
  }
  bool is_predefined() { return is_predefined_; }
  bool tested_for_equality() { return tested_for_equality_; }
  virtual void set_tested_for_equality() { tested_for_equality_ = true; }
  // Turn on the read bits of all fields, including nested fields
  // when recurse is true.
  virtual void SetAllFieldsRead(bool recurse);
  // Turn off the read bits of all fields, not recursive because types
  // are shared.
  virtual void ClearAllFieldsRead();
  // True if SetAllFieldsRead(true) has been called.
  bool AllFieldsRead() const { return fields_read_ == ALL_NESTED; }

  bool IsFullyNamed() const;

  // Type interface support
  virtual size_t size() const  { return sizeof(TupleVal*); }
  virtual TupleType* as_tuple()  { return this; }
  virtual void Visit(TypeVisitor *v);
  virtual void VisitChildren(TypeVisitor* v);

  // Proto support
  Field* field_for(int tag);  // returns NULL if no field with this tag

  // Runtime support
  TupleForm* form() const  { return form_; }

private:
  Scope* scope_;
  List<Field*> *fields_;  // excluding type and static declarations
  int nslots_;       // -1 before the fields have been bound to slots
  int ntotal_;       // -1 before the fields have been bound to slots
  bool is_finished_;
  bool is_predefined_;

  // NONE - no node has forced the top-level fields of "this" to be read.
  // Individual Field::read_ members may be set.
  // ALL  - all top-level fields of "this" are read.  Their Field::read_
  // members should all be set.  Implies nothing about nested tuples.
  // ALL_NESTED - all fields of "this" and nested tuples are read.  All of their
  // Field::read_ members should be set.
  // ClearAllFieldsRead sets the field to NONE.
  // SetAllFieldsRead can only change it to higher values.
  enum FieldsRead {NONE = 0, ALL = 1, ALL_NESTED = 2};
  FieldsRead fields_read_;

  bool tested_for_equality_;         // implies all fields are referenced
  // the following fields are for protocol buffers only:
  bool is_message_;  // true if the proto tuple corresponds to a parsed message
  bool is_auto_proto_;  // true if generated with MakeProto()
  int min_tag_;  // smallest tag of all fields
  List<int>* map_;  // maps each tag to a field index
  TupleVal* default_proto_val_;  // default proto value
  TupleForm* form_;

  virtual bool IsEqualType(Type* t, bool test_proto);
  void BindFieldsToSlots(Proc* proc);
  void AllocateTagMap(Proc* proc);
  void AllocateDefaultProto(Proc* proc);

  // Prevent construction from outside the class (must use factory method)
  TupleType() {}
};


// ArrayTypes represent aggregates of integer-indexed elements,
// all of which have the same type.
class ArrayType: public Type {
 public:
  // Creation
  static ArrayType* New(Proc* proc, Field* elem);

  // Creation, split into two parts so we have a type to which to refer
  // for recursive references.
  static ArrayType* NewUnfinished(Proc* proc, TypeName* tname,
                                  TupleType* enclosing_tuple);
  ArrayType* Finish(Proc* proc, Field* elem);

  // Make a proto array from the current array. The current
  // array is returned, if it is already a proto array, or if the
  // conversion fails (e.g. because the array contains a map).
  virtual Type* MakeProto(Proc* proc, ProtoForward* forward);

  virtual void set_tested_for_equality();

  // Turn on the read bits of all fields, including nested fields
  // when recurse is true.
  virtual void SetAllFieldsRead(bool recurse);
  // Turn off the read bits of all fields, not recursive because types
  // are shared.
  virtual void ClearAllFieldsRead();

  // Accessors
  virtual bool is_finished() const  { return is_finished_; }
  Field* elem() const  { return elem_; }
  const char* elem_name() const;
  virtual Type* elem_type() const;
  virtual bool is_proto() const  { return elem_type()->is_proto(); }

  // Type interface support
  virtual size_t size() const { return sizeof(ArrayVal*); }
  virtual ArrayType* as_array()  { return this; }
  virtual void Visit(TypeVisitor* v);
  virtual void VisitChildren(TypeVisitor* v);

  // Runtime support
  ArrayForm* form() const  { return form_; }

 private:
  Field* elem_;
  ArrayForm* form_;
  bool is_finished_;
  bool stop_recursion_;   // flag to prevent infinite recursive type traversal

  virtual bool IsEqualType(Type* t, bool test_proto);

  // Prevent construction from outside the class (must use factory method)
  ArrayType() {}
};


// A TableType specifies the aggregation method of an OutputType.
// New TableTypes can be registered with the SymbolTable and then
// the Sawzall parser will accept those new tables with OutputType
// specifications.
//
// Note: TableTypes are *not* regular Sawzall Types, but they are
// instead used to characterize OutputTypes.

class TableType {
 public:
  // Creation
  static TableType* New(Proc* proc, szl_string name, bool has_param,
                        bool has_weight);

  // accessors
  szl_string name() const  { return name_; }
  bool has_param() const  { return has_param_; }
  bool has_weight() const  { return has_weight_; }

  // comparer
  // false if a new definition would be inconsistent with this
  bool ConsistentWith(bool has_param, bool has_weight) {
    return has_param == has_param_ && has_weight == has_weight_;
  }

 private:
  szl_string name_;
  bool has_param_;
  bool has_weight_;

  // Prevent construction from outside the class (must use factory method)
  TableType() {}
};


// OutputType types represent possibly indexed types of output variables.
// OutputTypes are further characterized via TableTypes.
// The value of a variable of type OutputType is an integer index into the
// emitter table.
// Note that output types that use formats contain user-supplied expressions.
// (Function types also contain expressions as a way of specifying default
// arguments for intrinsics, but those expressions are always literals.)
// When an output type is used in an emit, the index and element fields are
// cloned as local variables and the format expressions are cloned.
// The effect is as if the expressions were anonymous functions, with the
// index and element fields as their parameters; then the functions are
// inlined when used in an emit.
// OutputType types may also contain a parameter expression that is used
// in emits.  These expressions are not cloned and do not participate in
// static analysis.
// TODO: either clone parameter expressions or move their evaluation
// to the point where the output type is declared; in either case, apply
// static analysis.

class OutputType: public Type {
 public:
  // Creation
  static OutputType* New(
    Proc* proc,
    // the table kind
    TableType* kind,
    // creation parameter expression, if any (NULL otherwise)
    Expr* param,
    // result of parameter expression evaluation (<0 if n/a or not yet known)
    int evaluated_param,
    // indices
    List<VarDecl*>* index_decls,
    // (possibly named) element
    VarDecl* elem_decl,
    // weight, if any (NULL otherwise)
    Field* weight,
    // file or proc attribute, if any, and its arguments
    bool is_proc, List<Expr*>* index_format_args,
    // format attribute, if any, and its arguments
    List<Expr*>* elem_format_args,
    // whether can be emitted in a static context
    bool is_static,
    // enclosing tuple type
    TupleType* enclosing_tuple
  );

  // Accessors
  TableType* kind() const  { return kind_; }
  Expr* param() const  { return param_; }
  int evaluated_param() const { return evaluated_param_; }
  void set_evaluated_param(int param) { evaluated_param_ = param; }
  bool is_evaluated_param() { return param_ == NULL || evaluated_param_ >= 0; }
  List<VarDecl*>* index_decls() const  { return index_decls_; }
  VarDecl* elem_decl() const  { return elem_decl_; }
  Type* elem_type() const;
  Field* weight() const  { return weight_; }
  bool is_proc() const  { return is_proc_; }
  List<Expr*>* index_format_args() const  { return index_format_args_; }
  List<Expr*>* elem_format_args() const  { return elem_format_args_; }
  bool uses_emitter() const  { return index_format_args_ == NULL; }
  bool is_static() const  { return is_static_; }

  // Type interface support
  virtual size_t size() const { return sizeof(IntVal*); }
  virtual OutputType* as_output()  { return this; }
  virtual void Visit(TypeVisitor *v);
  virtual void VisitChildren(TypeVisitor* v);

 private:
  TableType* kind_;
  Expr* param_;
  int evaluated_param_;  // result of param_ evaluation at compile- or run-time
  List<VarDecl*>* index_decls_;  // may be used in File(), so must be VarDecls
  VarDecl* elem_decl_;    // may be used in Format(), so must be a VarDecl
  Field* weight_;        // never used in an expression, so can be Field
  bool is_proc_;
  bool is_static_;
  List<Expr*>* index_format_args_;
  List<Expr*>* elem_format_args_;

  virtual bool IsEqualType(Type* t, bool test_proto);

  // Prevent construction from outside the class (must use factory method)
  OutputType() {}
};


class FunctionType: public Type {
 public:
  // Creation
  static FunctionType* New(Proc* proc);

  // Creation, split into two parts so we have a type to which to refer
  // for recursive references.
  static FunctionType* NewUnfinished(Proc* proc, TypeName* tname,
                                     TupleType* enclosing_tuple);
  FunctionType* Finish(Proc* proc);

  // Intrinsic/extension function signature creation.
  // These functions may only be called at initialization.
  // par: adds a mandatory parameter with (optional) name, and type
  // opt: adds an optional parameter
  // res: adds a result type
  // The functions conveniently return 'this', so they can be
  // chained. A typical use is (see e.g. intrinsic.cc):
  // (new FunctionType())->par(time_type)->opt(int_1)->res(time_type)
  FunctionType* par(szl_string name, Type* type);
  FunctionType* par(Type* type);
  FunctionType* opt(Expr* value);
  FunctionType* res(Type* type);

  // Accessors and setters
  virtual bool is_finished() const  { return is_finished_; }
  List<Field*>* parameters()  { return &parameters_; }
  void add_parameter(Field* field);
  Field* result() const  { return result_; }
  Type* result_type() const;
  void set_result(Field* field)  { result_ = field; }
  bool has_result() const  { return !result_type()->is_void(); }

  virtual size_t size() const  { return sizeof(FunctionVal*); }
  virtual FunctionType* as_function() { return this; }
  virtual void Visit(TypeVisitor *v);
  virtual void VisitChildren(TypeVisitor* v);

  // Compare parameter lists but not return type
  bool IsEqualParameters(FunctionType* t, bool test_proto);

  // Runtime support
  ClosureForm* form() const  { return form_; }

 private:
  bool is_finished_;
  bool is_predefined_;
  List<Field*> parameters_;
  Field* result_;
  ClosureForm* form_;

  virtual bool IsEqualType(Type* t, bool test_proto);

  // Prevent construction from outside the class (must use factory method)
  explicit FunctionType(Proc* proc) : parameters_(proc)  { }

 protected:
  // C++ compiler complains if there's no virtual destructor
  // however, no destructor is ever called...
  virtual ~FunctionType() { ShouldNotReachHere(); }
};


class MapType: public Type {
 public:
  // Creation
  static MapType* New(Proc* proc, Field* index, Field* elem);

  // Creation, split into two parts so we have a type to which to refer
  // for recursive references.
  static MapType* NewUnfinished(Proc* proc, TypeName* tname,
                                TupleType* enclosing_tuple);
  MapType* Finish(Proc* proc, Field* index, Field* elem);

  virtual void set_tested_for_equality();

  // Turn on the read bits of all fields, including nested fields
  // when recurse is true.
  virtual void SetAllFieldsRead(bool recurse);
  // Turn off the read bits of all fields, not recursive because types
  // are shared.
  virtual void ClearAllFieldsRead();

  // Accessors
  virtual bool is_finished() const  { return is_finished_; }
  Field* index() const  { return index_; }
  const char* index_name() const;
  Type* index_type() const;
  ArrayType* key_array_type() { return key_array_type_; }

  Field* elem() const  { return elem_; }
  const char* elem_name() const;
  virtual Type* elem_type() const;

  // Type interface support
  virtual size_t size() const  { return sizeof(MapVal*); }
  virtual MapType* as_map() { return this; }
  virtual void Visit(TypeVisitor *v);
  virtual void VisitChildren(TypeVisitor* v);

  // Runtime support
  MapForm* form() const  { return form_; }

 private:
  Field* index_;
  Field* elem_;
  ArrayType* key_array_type_;
  MapForm* form_;
  bool is_finished_;
  bool stop_recursion_;   // flag to prevent infinite recursive type traversal

  virtual bool IsEqualType(Type* t, bool test_proto);

  // Prevent construction from outside the class (must use factory method)
  MapType() {}
};


// A visitor pattern is used for type-specific dispatches. Each type T
// implements a Visit(TypeVisitor*) function which in turn calls the
// corresponding TypeVisitor::DoT(T*) function. Concrete visitors
// are subclasses of the abstract class TypeVisitor.
// Each type also implements a VisitChildren(TypeVisitor*) function
// which calls Visit() on each type from which it composed.
//
class TypeVisitor {
 public:
  virtual void DoType(Type* x) = 0;
  virtual void DoBadType(BadType* x)  { DoType(x); }
  virtual void DoBasicType(BasicType* x)  { DoType(x); }
  virtual void DoIncompleteType(IncompleteType* x)  { DoType(x); }
  virtual void DoArrayType(ArrayType* x)  { DoType(x); }
  virtual void DoFunctionType(FunctionType* x)  { DoType(x); }
  virtual void DoMapType(MapType* x)  { DoType(x); }
  virtual void DoOutputType(OutputType* x)  { DoType(x); }
  virtual void DoTupleType(TupleType* x)  { DoType(x); }

  virtual ~TypeVisitor() {}
};


}  // namespace sawzall
