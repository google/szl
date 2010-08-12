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

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/tracer.h"
#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/convop.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/parser.h"
#include "engine/proc.h"
#include "engine/ir.h"

namespace sawzall {


bool IR::IsLvalue(Expr* x) {
  // avoid complaints in case of a bad x
  return x->AsBadExpr() != NULL || RootVar(x) != NULL;
}


bool IR::IsStaticLvalue(Expr* x) {
  if (x->AsBadExpr() != NULL)
    return false;  // avoid complaints
  if (x->AsVariable() != NULL)
    return x->AsVariable()->is_static();
  if (x->AsIndex() != NULL)
    return IsStaticLvalue(x->AsIndex()->var());
  if (x->AsSelector() != NULL)
    return IsStaticLvalue(x->AsSelector()->var());
  // all other cases are not static l-values
  // (may be conservative)
  return false;
}


Variable* IR::RootVar(Expr* x) {
  if (x->AsVariable() != NULL)
    return x->AsVariable();
  if (x->AsIndex() != NULL)
    return RootVar(x->AsIndex()->var());
  if (x->AsSlice() != NULL)
    return RootVar(x->AsSlice()->var());
  if (x->AsSelector() != NULL)
    return RootVar(x->AsSelector()->var());
  // all other cases don't have a root variable
  // (may be conservative)
  return NULL;
}


bool IR::IsCompatibleOp(Type* type, Binary::Op op) {
  // bad types are always ok
  if (type->is_bad()) return true;

  switch (op) {
    // comparisons are always ok
    case Binary::EQL:
    case Binary::NEQ:
    case Binary::LSS:
    case Binary::LEQ:
    case Binary::GTR:
    case Binary::GEQ:
      return true;

    // logicals only apply to booleans
    case Binary::LAND:
    case Binary::LOR:
    case Binary::AND:
    case Binary::OR:
      return type->is_bool();

    // simple arithmetic: int float time
    // add is special for several types
    case Binary::ADD:
      if (type->is_fingerprint() || type->is_indexable())
        return true;
      // fall through
    case Binary::SUB:
    case Binary::MUL:
    case Binary::DIV:
      return type->is_int() || type->is_uint() || type->is_float() || type->is_time();

    // must be integers
    case Binary::MOD:
    case Binary::BAND:
    case Binary::BOR:
    case Binary::BXOR:
    case Binary::SHL:
    case Binary::SHR:
      return type->is_int() || type->is_uint();
  }

  ShouldNotReachHere();
  return false;
}


bool IR::IsCompatibleExpr(Proc* proc, Type* type, Expr* x) {
  // try to type incompletely typed composites first
  Composite* c = x->AsComposite();
  if (c != NULL && c->type()->is_incomplete())
    SetCompositeType(proc, c, type);
  // to be compatible x->type() must be the same type
  return x->type()->IsEqual(type, false);
}


static bool SuppliedFunctionArgsAreCompatible(Proc* proc, List<Field*>* params,
                                              const List<Expr*>* args) {
  // Must have enough args for formal parameter list,
  // but may have less if some parameters are optional.
  assert(args->length() <= params->length());

  // Check supplied arguments.
  for (int i = 0; i < args->length(); i++) {
    Field* param = params->at(i);
    // This is the one place we allow any_tuple_type to appear.
    // It represents a (usually proto) tuple argument to an intrinsic
    // function. It cannot be created by the user, only by the
    // intrinsic registration mechanism.  Note that we use pointer
    // equality to check for any_tuple_type; it is unique and
    // has no contents.
    if (param->type()->as_tuple() == SymbolTable::any_tuple_type()) {
      if (args->at(i)->type()->as_tuple() == NULL)
        return false;
      // else it's a tuple and we accept it.
    } else if (!IR::IsCompatibleExpr(proc, param->type(), args->at(i))) {
      return false;
    }
  }
  return true;
}


bool IR::IsMatchingFunctionArgList(Proc* proc, FunctionType* type,
                                              const List<Expr*>* args) {
  List<Field*>* params = type->parameters();
  return args->length() == params->length()
      && SuppliedFunctionArgsAreCompatible(proc, params, args);
}


bool IR::IsCompatibleFunctionArgList(Proc* proc, FunctionType* type,
                                     List<Expr*>* args) {
  List<Field*>* params = type->parameters();
  if (args->length() > params->length()
      || !SuppliedFunctionArgsAreCompatible(proc, params, args)) {
    return false;
  }

  // If too few arguments, append optional argument values.
  for (int i = args->length(); i < params->length(); i++) {
    Field* param = params->at(i);
    if (!param->has_value())
      return false;  // a mandatory argument was not supplied
    args->Append(param->value());
  }

  return true;
}


// Check if c is assignment-compatible with an indexable type.
static bool IsCompatibleWithIndexable(Proc* proc, Composite* c, Type* type) {
  assert(type->is_indexable());
  // a non-paired composite can be assigned to an
  // indexable type if the element types are compatible
  if (!c->has_pairs()) {
    Type* elem_type = type->elem_type();
    // make sure all composite entries are compatible
    // and rewrite their types if possible - empty composites
    // are trivially compatible
    for (int i = 0; i < c->length(); i++)
      if (!IR::IsCompatibleExpr(proc, elem_type, c->at(i)))
        return false;
    // compatible
    return true;
  }
  // not compatible
  return false;
}


// Check if c is assignment-compatible with a map type.
static bool IsCompatibleWithMap(Proc* proc, Composite* c, MapType* type) {
  // a paired composite can be assigned to a
  // map type if the element types are compatible
  if (c->has_pairs()) {
    // empty composites are trivially compatible
    if (!c->is_empty()) {
      // non-empty composites are compatible
      // if all the entries are compatible
      Type* key_type = type->index_type();
      Type* value_type = type->elem_type();
      for (int i = 0; i < c->length(); i += 2)
        if (!(IR::IsCompatibleExpr(proc, key_type, c->at(i + 0)) &&
              IR::IsCompatibleExpr(proc, value_type, c->at(i + 1))))
          return false;
    }
    // compatible
    return true;
  }
  // not compatible
  return false;
}


// Check if c is assignment-compatible with a tuple type.
static bool IsCompatibleWithTuple(Proc* proc, Composite* c, TupleType* type) {
  // a non-paired composite can be assigned to a
  // tuple type if the element types are compatible
  // treat unfinished tuple types as incompatible
  if (type->is_finished() && !c->has_pairs()) {
    List<Field*>* fields = type->fields();
    // the number of fields must correspond for a match
    if (c->length() == fields->length()) {
      for (int i = 0; i < c->length(); i++)
        if (!IR::IsCompatibleExpr(proc, fields->at(i)->type(), c->at(i)))
          return false;
      return true;
    }
  }
  // not compatible
  return false;
}


static Type* GetCompositeElementType(Type* type, Expr* x) {
  if (type == NULL && !x->type()->is_incomplete())
    type = x->type();
  return type;
}


static bool SetMapCompositeType(Proc* proc, Composite* c) {
  assert(c->has_pairs());
  // find the first key and value types that are defined
  Type* key_type = NULL;
  Type* value_type = NULL;
  for (int i = 0; i < c->length() && (key_type == NULL || value_type == NULL); i += 2) {
    key_type = GetCompositeElementType(key_type, c->at(i + 0));
    value_type = GetCompositeElementType(value_type, c->at(i + 1));
  }
  // if missing the key and/or value type, try composites
  for (int i = 0; i < c->length() && (key_type == NULL || value_type == NULL); i += 2) {
    Expr* key = c->at(i + 0);
    Expr* value = c->at(i + 1);
    if (key_type == NULL && key->AsComposite() != NULL &&
        key->type()->is_incomplete())
      if (IR::DetermineCompositeType(proc, key->AsComposite(), true))
        key_type = key->type();
    if (value_type == NULL && value->AsComposite() != NULL &&
        value->type()->is_incomplete())
      if (IR::DetermineCompositeType(proc, value->AsComposite(), true))
        value_type = value->type();
  }
  // if we have a key and value type, check if the entire composite complies
  if (key_type != NULL && value_type != NULL) {
    for (int i = 0; i < c->length(); i += 2)
      if (!IR::IsCompatibleExpr(proc, key_type, c->at(i + 0)) || !IR::IsCompatibleExpr(proc, value_type, c->at(i + 1)))
        return false;
    // the entire composite complies => set its type
    Field* key_field = Field::New(proc, c->file_line(), NULL, key_type);
    Field* value_field = Field::New(proc, c->file_line(), NULL, value_type);
    c->set_type(MapType::New(proc, key_field, value_field));
    return true;
  }
  // couldn't set the type
  assert(c->type()->is_incomplete());
  return false;
}


static bool SetIndexableCompositeType(Proc* proc, Composite* c) {
  assert(!c->has_pairs());
  // find the first element type that is defined
  Type* elem_type = NULL;
  for (int i = 0; i < c->length() && elem_type == NULL; i++) {
    elem_type = GetCompositeElementType(elem_type, c->at(i));
  }
  // if none found, try composites
  for (int i = 0; i < c->length() && elem_type == NULL; i++) {
    Expr* elem = c->at(i);
    if (elem->AsComposite() != NULL && elem->type()->is_incomplete())
      if (IR::DetermineCompositeType(proc, elem->AsComposite(), true))
        elem_type = elem->type();
  }
  // if we have an element type, check if the entire composite complies
  if (elem_type != NULL) {
    for (int i = 0; i < c->length(); i++)
      if (!IR::IsCompatibleExpr(proc, elem_type, c->at(i)))
        return false;
    // the entire composite complies => set its type
    Field* f = Field::New(proc, c->file_line(), NULL, elem_type);
    c->set_type(ArrayType::New(proc, f));
    return true;
  }
  // couldn't set the type
  assert(c->type()->is_incomplete());
  return false;
}


static bool SetTupleCompositeType(Proc* proc, Composite* c) {
  assert(!c->has_pairs());
  Scope* scope = Scope::New(proc);
  for (int i = 0; i < c->length(); i++) {
    Expr* elem = c->at(i);
    // don't forget nested composites
    if (elem->AsComposite() != NULL && elem->type()->is_incomplete())
      if (!IR::DetermineCompositeType(proc, elem->AsComposite(), true))
        return false;
    // add a field for each composite element
    Field* f = Field::New(proc, c->file_line(), NULL, elem->type());
    scope->InsertOrDie(f);
  }
  c->set_type(TupleType::New(proc, scope, false, false, false));
  return true;
}


bool IR::SetCompositeType(Proc* proc, Composite* c, Type* type) {
  assert(c != NULL && c->type()->is_incomplete());
  // try to set the composite's type to "type"
  if (type->is_indexable()) {
    if (IsCompatibleWithIndexable(proc, c, type)) {
      c->set_type(type);
      return true;
    }
  } else if (type->is_tuple()) {
    if (IsCompatibleWithTuple(proc, c, type->as_tuple())) {
      c->set_type(type);
      return true;
    }
  } else if (type->is_map()) {
    if (IsCompatibleWithMap(proc, c, type->as_map())) {
      c->set_type(type);
      return true;
    }
  }
  return false;
}


bool IR::DetermineCompositeType(Proc* proc, Composite* c, bool allow_tuples) {
  assert(c != NULL && c->type()->is_incomplete());
  // try to determine the appropriate anonymous type for the composite
  if (c->has_pairs())
    return SetMapCompositeType(proc, c);
  else
    return SetIndexableCompositeType(proc, c) ||
           (allow_tuples && SetTupleCompositeType(proc, c));
}


bool IR::TupleContainsItself(TupleType* tuple_type, Field* field) {
  assert(tuple_type->type_name() != NULL);
  // Only need to check type identity, not equality.
  // (Recursion only occurs when referencing the type itself.)
  if (field->type() == tuple_type)
    return true;
  // Ignore recursive types other than the tuple being checked.
  // (Currently impossible, but allow for adding nested type declarations later)
  if (field->recursive())
    return false;

  if (field->type()->is_tuple()) {
    List<Field*>* fields = field->type()->as_tuple()->fields();
    for (int i = fields->length(); i-- > 0; )
      if (IR::TupleContainsItself(tuple_type, fields->at(i)))
        return true;
  }
  return false;
}


// table of ConversionOps to convert basic type to basic type
// (e.g.: convtab[string][int] == string2int)
static ConversionOp convtab[BasicType::NBasic][BasicType::NBasic] = {
  // to: bool bytes fingerprint float int string time uint
  { noconv, noconv, noconv, noconv, typecast, bool2str, noconv, bits2uint }, // from: bool
  { noconv, noconv, bytes2fpr, noconv, bytes2int, bytes2str, noconv, bytes2uint }, // from: bytes
  { noconv, fpr2bytes, noconv, noconv, typecast, fpr2str, noconv, bits2uint }, // from: fingerprint
  { noconv, noconv, noconv, noconv, float2int, float2str, noconv, float2uint }, // from: float
  { noconv, int2bytes, typecast, int2float, noconv, int2str, typecast, bits2uint }, // from: int
  { str2bool, str2bytes, str2fpr, str2float, str2int, noconv, str2time, str2uint }, // from: string
  { noconv, noconv, noconv, noconv, typecast, time2str, noconv, bits2uint }, // from: time
  { noconv, uint2bytes, uint2fpr, uint2float, uint2int, uint2str, uint2time, noconv }, // from: uint
};


static Expr* CreateArrayToTupleConversion(
  Parser* parser, FileLine* fl, TupleType* tuple_type, ArrayType* array_type,
  Expr* src, List<Expr*>* params, bool implicit) {

  CHECK(tuple_type->is_finished());
  if (!params->is_empty())
    return NULL;  // TODO?  may be too hard, because of different tuple fields

  Proc* proc = parser->proc();

  List<Field*>* fields = tuple_type->fields();
  Composite* csrc = src->AsComposite();
  const int n = fields->length();

  // the number of array elements must correspond
  // to the number of tuple fields - if the src array is
  // a composite, we can check this at compile time
  if (csrc != NULL && csrc->length() != n) {
    // number of elements and fields doesn't match => conversion failed
    return NULL;
  }

  // do not evaluate the source multiple times
  Expr* index_var = src;
  // When src is a Variable, we could clone it instead of using a temp.
  if (csrc == NULL)
    index_var = parser->CreateTempVar(src);

  Composite* comp = Composite::New(proc, fl);
  comp->set_type(tuple_type);
  // Mark composites created by an explicit conversion, so the
  // conversion can be reconstructed at printing.
  comp->set_has_conversion(!implicit);
  for (int i = 0; i < fields->length(); i++) {
    Field* field = fields->at(i);
    Expr* src_elem;
    if (csrc != NULL)
      src_elem = csrc->at(i);
    else
      src_elem = Index::New(proc, fl, index_var,
                            Literal::NewInt(proc, fl, NULL, i), NULL);
    const bool no_warning = false;  // if the conversion is not required
    Expr* dst_elem = IR::CreateConversion(parser, fl, field->type(), src_elem,
                                          params->Copy(proc), no_warning,
                                          true);
    if (dst_elem != NULL)
      // add converted element
      comp->Append(dst_elem);
    else
      // cannot convert element => terminate conversion loop
    return NULL;
  }

  // if the src array is not a composite (and thus its length is
  // not known at compile time) introduce an explicit guard
  // to verify at runtime that the number of array elements
  // matches the number of tuple fields
  if (csrc == NULL) {
    // construct guard condition: (len(src) == n)
    // 1) len(src)
    List<Expr*>* args = List<Expr*>::New(proc);
    args->Append(index_var);
    Call* len =
      Call::New(proc, fl, SymbolTable::universe()->LookupOrDie("len")->
                AsIntrinsic(), args);
    // 2) n
    Literal* lit = Literal::NewInt(proc, fl, NULL, n);
    // 3) len(src) == n
    Binary* guard =
      Binary::New(proc, fl, SymbolTable::bool_type(), len,
                  Binary::EQL, eql_bits, lit);
    // guard the conversion
    // (the guard is evaluated _before_ the expression)
    const char* msg = proc->
      PrintString("array -> tuple conversion failed: len(%N) != %d", src, n);
    return RuntimeGuard::New(proc, fl, guard, comp, msg);
  }

  return comp;
}


static bool CheckConversion(Parser* parser, Type* dst_type, Type* src_type,
                            Expr* src, ConversionOp* op) {
  if (src_type->is_bad() || dst_type->is_bad()) {
    // try to avoid additional errors
    return true;
  } else if (src_type->is_bytes() && dst_type->is_tuple() &&
             dst_type->is_proto()) {
    // special case: conversion of bytes (proto buffer format) -> tuple
    // remember the first named proto type to which "input" was converted
    if (src->AsVariable() != NULL)
      parser->CheckForInputProtoConversion(src->AsVariable(),
                                           dst_type->as_tuple());
    *op = bytes2proto;

  } else if (src_type->is_tuple() && src_type->is_proto() &&
             dst_type->is_bytes()) {
    // special case: conversion of tuple -> bytes (proto buffer format)
    *op = proto2bytes;

  } else if (src_type->IsEqual(dst_type, true)) {
    *op = noconv;
  } else if (dst_type->is_basic() && src_type->is_basic()) {
    // convtab does not contain entries for VOID; check against actual size
    if (src_type->as_basic()->kind() >= ARRAYSIZE(convtab) ||
        dst_type->as_basic()->kind() >= ARRAYSIZE(convtab[0]))
      return false;
    *op = convtab[src_type->as_basic()->kind()][dst_type->as_basic()->kind()];
    if (*op == noconv)
      return false;
  } else if (dst_type->IsEqual(SymbolTable::array_of_int_type(), false) &&
             src_type->is_string()) {
    *op = str2array;
  } else if (dst_type->is_string()) {
      if (src_type->is_array()) {
        *op = array2str;
      } else if (src_type->is_map()) {
        *op = map2str;
      } else if (src_type->is_tuple()) {
        *op = tuple2str;
      } else if (src_type->is_function()) {
        *op = function2str;
      } else {
        return false;
      }
  } else if (dst_type->is_tuple() && src_type->is_tuple() &&
             src_type->IsEqual(dst_type, false)) {
    // => tuples are structurally equal but proto types are different
    *op = tuple2tuple;
  } else {
    // this conversion is not supported.
    return false;
  }
  return true;
}


static bool CheckExtraParams(Parser* parser, Type* src_type, Expr* src,
                             Type* full_dst_type, List<Expr*>* params,
                             bool params_allowed, bool implicit,
                             ConversionOp op) {
  // Supply missing extra parameters, check type of supplied extra
  // parameters, check unallowed extra parameters.
  int nparams = params->length();
  BasicType::Kind param_kind = BasicType::VOID;
  if (params_allowed) {
    switch (op) {
      case noconv:
      case typecast:
      case int2float:
      case str2bool:
      case fpr2bytes:
      case str2float:
      case uint2fpr:
      case uint2time:
      case float2int:
      case uint2int:
      case bool2str:
      case bits2uint:
      case float2uint:
      case fpr2str:
      case float2str:
      case uint2float:
      case bytes2fpr:
      case function2str:
      case bytes2proto:
      case proto2bytes:
      case tuple2tuple:
        param_kind = BasicType::VOID;  // no extra params allowed
        break;

      case str2bytes:
      case bytes2str:
        if (nparams == 0)
          params->Append(SymbolTable::string_utf8());
        param_kind = BasicType::STRING;
        break;

      case str2int:
      case str2uint:
        if (nparams == 0) {
          // int("08") fails because implicitly the base is 0 meaning
          // autodetect, and 8 is not a valid octal character.
          // Quite often users will have wanted a decimal
          // conversion, but didn't know to say int("08", 10).
          // Only emit the warning for explicit conversions since
          // only explicit conversions can have a base argument.
          if (!implicit)
            parser->Warning("no base provided for conversion to integer; "
                            "base will be input-dependent");
          params->Append(SymbolTable::int_0());
        }
        param_kind = BasicType::INT;
        break;

      case str2fpr:
        if (nparams == 0)
          params->Append(SymbolTable::int_0());
        param_kind = BasicType::INT;
        break;

      case int2str:
      case uint2str:
        if (nparams == 0)
          params->Append(SymbolTable::int_10());
        param_kind = BasicType::INT;
        break;

      case time2str:
      case str2time:
        if (nparams == 0)
          params->Append(SymbolTable::empty_string());
        param_kind = BasicType::STRING;
        break;

      case array2str:
        // conversion from array of int to string is a special case
        // (code points), and an explicit parameter is allowed.
        if (src_type->IsEqual(SymbolTable::array_of_int_type(), false)) {
          if (nparams == 0)
            params->Append(SymbolTable::empty_string());
          param_kind = BasicType::STRING;
          break;
        }
        // fall through to the normal case.

      case map2str:
      case tuple2str:
        // parameter not allowed but defaults to empty string
        if (nparams == 0) {
          params->Append(SymbolTable::empty_string());
          param_kind = BasicType::STRING;
        } else {
          param_kind = BasicType::VOID;  // explicit parameter, generate error
        }
        break;

      case str2array:
        param_kind = BasicType::STRING;  // required, must be "unicode"
        break;

      case int2bytes:
      case uint2bytes:
      case bytes2int:
      case bytes2uint:
        param_kind = BasicType::STRING;  // required parameter
        break;
    }
  } else {
    switch (op) {
      case int2bytes:
      case uint2bytes:
      case bytes2int:
      case bytes2uint:  // parameters are required.
        return false;

      default:
        break;
    }
  }
  if (param_kind == BasicType::VOID) {
    // extra parameter is not allowed.
    if (params->length() != 0) {
      parser->Error("no parameters allowed for conversion convert(%T, %N)",
                    full_dst_type, src);
      params->Clear();
    }
  } else {
    // one (possibly default) extra parameter required, check its type.
    // TODO: emit error here, where we know what is wrong?
    if (params->length() != 1 || !params->at(0)->type()->is_basic() ||
        params->at(0)->type()->as_basic()->kind() != param_kind)
      return false;
  }
  return true;
}


Expr* IR::CreateConversion(Parser* parser, FileLine* fl, Type* type,
                           Expr* src, List<Expr*>* params,
                           bool warning, bool implicit) {
  assert(params != NULL);
  Proc* proc = parser->proc();
  int source_param_count = params->length();

  if (type->is_tuple() && !type->as_tuple()->is_finished()) {
    parser->Error("an object of tuple type %T cannot be used in a way that "
                  "requires its complete type before the tuple has been "
                  "completed", type);
    return BadExpr::New(proc, fl, src);
  }

  // handle composites with incomplete types
  { Composite* c = src->AsComposite();
    if (c != NULL && c->type()->is_incomplete()) {
      if (SetCompositeType(proc, c, type)) {
        // If they are compatible with the target type, no conversion is needed,
        // but set the flag to make a note that there was one.
        // Also do not complain with warning, because incompletely typed
        // composites can always be legally converted to a compatible type.
        c->set_has_conversion(!implicit);
        return src;
      } else {
        // not compatible; try to guess a type for the composite.
        // TODO: consider restricting this to indexable (arrays)
        // unless the destination type is string, since there are no map or
        // tuple conversions except to string.  (Tuple conversions that just
        // change proto/tuple are handled by SetCompositeType.)
        DetermineCompositeType(proc, c, true);
      }
    }
  }

  Type* src_type = src->type();
  Expr* result = NULL;

  // Array to tuple is generated as a series of individual conversions,
  // and so is not suitable for array to array and array to map.
  if (src_type->is_array() && type->is_tuple()) {
    ArrayType* atype = src_type->as_array();
    // Would also set all fields of tuple type written if tracking writes
    // We do the lower-level conversion as implicit even if the top-level
    // conversion was not, since there is no way for the user to pass
    // convert() arguments into it anyway.
    result = CreateArrayToTupleConversion(
      parser, fl, type->as_tuple(), atype, src, params, implicit);
  } else {
    bool ok = true;
    Conversion::Kind kind;
    ConversionOp convop = noconv;
    ConversionOp key_convop = noconv;

    // Array to map.
    if (src_type->is_array() && type->is_map()) {
      kind = Conversion::kArrayToMapConv;
      Type* src_elem_type = src_type->as_array()->elem_type();
      Type* dst_index_type = type->as_map()->index_type();
      Type* dst_elem_type = type->as_map()->elem_type();
      ok &= CheckConversion(parser, dst_elem_type, src_elem_type, src, &convop);
      ok &= CheckConversion(parser, dst_index_type, src_elem_type, src,
                            &key_convop);
      ok &= ImplementedArrayToMapConversion(convop);
      ok &= ImplementedArrayToMapConversion(key_convop);
      if (ok) {
        ok &= CheckExtraParams(parser, src_elem_type, src, type, params, false,
                               implicit, convop);
        ok &= CheckExtraParams(parser, src_elem_type, src, type, params, false,
                               implicit, key_convop);
      }

    // Array to array.
    } else if (src_type->is_array() && type->is_array()) {
      kind = Conversion::kArrayToArrayConv;
      Type* src_elem_type = src_type->as_array()->elem_type();
      Type* dst_elem_type = type->as_array()->elem_type();
      ok &= CheckConversion(parser, dst_elem_type, src_elem_type, src, &convop);
      ok &= ImplementedArrayToArrayConversion(convop);
      if (ok) {
        ok &= CheckExtraParams(parser, src_elem_type, src, type, params, true,
                               implicit, convop);
      }

    // Basic.
    } else {
      kind = Conversion::kBasicConv;
      ok &= CheckConversion(parser, type, src_type, src, &convop);
      if (ok) {
        ok &= CheckExtraParams(parser, src_type, src, type, params, true,
                               implicit, convop);
      }
    }

    if (ok) {
      if (kind == Conversion::kBasicConv && convop == noconv)
        result = src;
      else
        result = Conversion::New(proc, fl, type, src, params,
                                 source_param_count, kind, convop, key_convop);
    }
  }

  if (result == src && warning) {
    // no conversion was required: issue a warning
    parser->Warning("%N already of type %T; conversion suppressed", src, type);
  }

  // done
  if (result != NULL) {
    return result;
  } else {
    // we can't convert
    parser->Error("cannot convert %N (type %T) to %T", src, src_type, type);
    return BadExpr::New(proc, fl, src);
  }
}


// table of opcodes by type and symbol.
static struct OpcodeTab {
  bool (Type::*test)() const;
  Symbol sym;
  Opcode op;
} opcodetab[] = {
  // types listed in decreasing order of likely frequency
  { &Type::is_int, PLUS, add_int },
  { &Type::is_int, MINUS, sub_int },
  { &Type::is_int, TIMES, mul_int },
  { &Type::is_int, DIV, div_int },
  { &Type::is_int, MOD, mod_int },
  { &Type::is_int, SHL, shl_int },
  { &Type::is_int, SHR, shr_int },
  { &Type::is_int, BITAND, and_int },
  { &Type::is_int, BITOR, or_int },
  { &Type::is_int, BITXOR, xor_int },
  { &Type::is_int, EQL, eql_bits },
  { &Type::is_int, NEQ, neq_bits },
  { &Type::is_int, LSS, lss_int },
  { &Type::is_int, LEQ, leq_int },
  { &Type::is_int, GTR, gtr_int },
  { &Type::is_int, GEQ, geq_int },

  { &Type::is_bytes, PLUS, add_bytes },
  { &Type::is_bytes, EQL, eql_bytes },
  { &Type::is_bytes, NEQ, neq_bytes },
  { &Type::is_bytes, LSS, lss_bytes },
  { &Type::is_bytes, LEQ, leq_bytes },
  { &Type::is_bytes, GTR, gtr_bytes },
  { &Type::is_bytes, GEQ, geq_bytes },

  { &Type::is_string, PLUS, add_string },
  { &Type::is_string, EQL, eql_string },
  { &Type::is_string, NEQ, neq_string },
  { &Type::is_string, LSS, lss_string },
  { &Type::is_string, LEQ, leq_string },
  { &Type::is_string, GTR, gtr_string },
  { &Type::is_string, GEQ, geq_string },

  { &Type::is_float, PLUS, add_float },
  { &Type::is_float, MINUS, sub_float },
  { &Type::is_float, TIMES, mul_float },
  { &Type::is_float, DIV, div_float },
  { &Type::is_float, EQL, eql_float },
  { &Type::is_float, NEQ, neq_float },
  { &Type::is_float, LSS, lss_float },
  { &Type::is_float, LEQ, leq_float },
  { &Type::is_float, GTR, gtr_float },
  { &Type::is_float, GEQ, geq_float },

  { &Type::is_bool, EQL, eql_bits },
  { &Type::is_bool, NEQ, neq_bits },
  { &Type::is_bool, CONDAND, nop },  // translated into branches - use nop
  { &Type::is_bool, CONDOR, nop },  // translated into branches - use nop
  { &Type::is_bool, AND, and_bool },
  { &Type::is_bool, OR, or_bool },

  { &Type::is_fingerprint, PLUS, add_fpr },
  { &Type::is_fingerprint, EQL, eql_bits },
  { &Type::is_fingerprint, NEQ, neq_bits },

  { &Type::is_uint, PLUS, add_uint },
  { &Type::is_uint, MINUS, sub_uint },
  { &Type::is_uint, TIMES, mul_uint },
  { &Type::is_uint, DIV, div_uint },
  { &Type::is_uint, MOD, mod_uint },
  { &Type::is_uint, SHL, shl_uint },
  { &Type::is_uint, SHR, shr_uint },
  { &Type::is_uint, BITAND, and_uint },
  { &Type::is_uint, BITOR, or_uint },
  { &Type::is_uint, BITXOR, xor_uint },
  { &Type::is_uint, EQL, eql_bits },
  { &Type::is_uint, NEQ, neq_bits },
  { &Type::is_uint, LSS, lss_bits },
  { &Type::is_uint, LEQ, leq_bits },
  { &Type::is_uint, GTR, gtr_bits },
  { &Type::is_uint, GEQ, geq_bits },

  { &Type::is_time, PLUS, add_time },
  { &Type::is_time, MINUS, sub_time },
  { &Type::is_time, EQL, eql_bits },
  { &Type::is_time, NEQ, neq_bits },
  { &Type::is_time, LSS, lss_bits },
  { &Type::is_time, LEQ, leq_bits },
  { &Type::is_time, GTR, gtr_bits },
  { &Type::is_time, GEQ, geq_bits },

  { &Type::is_array, PLUS, add_array },
  { &Type::is_array, EQL, eql_array },
  { &Type::is_array, NEQ, neq_array },

  { &Type::is_map, EQL, eql_map },
  { &Type::is_map, NEQ, neq_map },

  { &Type::is_tuple, EQL, eql_tuple },
  { &Type::is_tuple, NEQ, neq_tuple },

  { &Type::is_function, EQL, eql_closure },
  { &Type::is_function, NEQ, neq_closure },
};


Opcode IR::OpcodeFor(Symbol sym, Type* type) {
  // ignore bad types
  if (type->is_bad())
    return illegal;
  for (int i = 0; opcodetab[i].op != illegal; i++)
    if (sym == opcodetab[i].sym && (type->*opcodetab[i].test)())
      return opcodetab[i].op;
  // no matching opcode found
  return illegal;
}


}  // namespace sawzall
