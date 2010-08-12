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

#include <time.h>
#include <assert.h>

#include "emitvalues/sawzall.pb.h"

#include "engine/globals.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "utilities/strutils.h"
#include "utilities/szlmutex.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/proc.h"
#include "engine/protocolbuffers.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/backendtype.h"

namespace sawzall {


// -----------------------------------------------------------------------------
// Type conversion between Backend and Sawzall types


// Sawzall -> Backend

static const char* BackendNameFor(szl_string name) {
  return name == NULL ? "" : name;
}


static void SetField(SzlFieldProto* fpb, szl_string name, Type* type) {
  fpb->set_label(BackendNameFor(name));
  fpb->set_type(BackendTypeFor(type));
}


static void SetField(SzlFieldProto* fpb, Field* field) {
  if (field->recursive())
    FatalError("backend support for recursive type %T is not implemented: "
               "do not use recursive types in table declarations",
               field->type());
  SetField(fpb, field->name(), field->type());
  if (field->has_tag()) fpb->set_tag(field->tag());
}


static SzlTypeProto::KIND BackendKindFor(BasicType::Kind kind) {
  switch (kind) {
    case BasicType::BOOL:
      return SzlTypeProto::BOOL;
    case BasicType::BYTES:
      return SzlTypeProto::BYTES;
    case BasicType::FINGERPRINT:
      return SzlTypeProto::FINGERPRINT;
    case BasicType::FLOAT:
      return SzlTypeProto::FLOAT;
    case BasicType::INT:
      // fall through
    case BasicType::UINT:
      return SzlTypeProto::INT;
    case BasicType::STRING:
      return SzlTypeProto::STRING;
    case BasicType::TIME:
      return SzlTypeProto::TIME;
    case BasicType::VOID:
      // fall through
    default:
      ShouldNotReachHere();
      return SzlTypeProto::VOID;
  }
}


string BackendTypeFor(Type* type) {
  SzlTypeProto typb;
  assert(type != NULL);

  if (type->is_basic()) {
    SzlTypeProto::KIND kind = BackendKindFor(type->as_basic()->kind());
    typb.set_kind(kind);

  } else if (type->is_tuple()) {
    // 1) create the backend type
    typb.set_kind(SzlTypeProto::TUPLE);
    // 2) add fields
    { List<Field*>* fields = type->as_tuple()->fields();
      for (int i = 0; i < fields->length(); i++) {
        Field* field = fields->at(i);
        SetField(typb.add_fields(), field);
      }
    }
    // 3) done

  } else if (type->is_array()) {
    // 1) create the backend type
    typb.set_kind(SzlTypeProto::ARRAY);
    // 2) add element
    SetField(typb.mutable_element(), type->as_array()->elem());
    // 3) done

  } else if (type->is_map()) {
    MapType* mtype = type->as_map();
    // 1) create the backend type
    typb.set_kind(SzlTypeProto::MAP);
    // 2) add index
    SetField(typb.add_indices(), mtype->index());
    // 3) add element
    SetField(typb.mutable_element(), mtype->elem());
    // 4) done

  } else if (type->is_output()) {
    OutputType* otype = type->as_output();
    // 1) create the backend type
    typb.set_kind(SzlTypeProto::TABLE);
    typb.set_table(otype->kind()->name());
    // 2) set parameter
    typb.set_param(otype->evaluated_param());
    // 3) add indices
    { List<VarDecl*>* index_decls = otype->index_decls();
      for (int i = 0; i < index_decls->length(); i++) {
        VarDecl* index_decl = index_decls->at(i);
        SetField(typb.add_indices(), index_decl->name(), index_decl->type());
      }
    }
    // 4) specify element field
    // note: if an element format attribute is present, the element
    // type is string independent of the actual element type
    { Type* elem_type = otype->elem_type();
      if (otype->elem_format_args() != NULL)
        elem_type = SymbolTable::string_type();
      SetField(typb.mutable_element(), otype->elem_decl()->name(), elem_type);
    }
    // 5) specify optional weight field
    if (otype->weight() != NULL)
      SetField(typb.mutable_weight(), otype->weight());
    // 6) done

  } else if (type->is_function()) {
    // At this point this conversion function is only used for tables and they
    // do not support function types as its fields. If this ever changes or
    // the function is used elsewhere, the ShouldNotReachHere should be
    // removed and tests for this section of the function should be added.
    ShouldNotReachHere();
    FunctionType* ftype = type->as_function();
    // 1) create the backend type
    typb.set_kind(SzlTypeProto::FUNCTION);
    // 2) set parameters
    { List<Field*>* params = ftype->parameters();
      for (int i = 0; i < params->length(); i++) {
        Field* param = params->at(i);
        szl_string name = "";
        if (param->has_name())
          name = param->name();
        SetField(typb.add_fields(), name, param->type());
      }
    }
    // 3) specify optional result
    if (ftype->has_result())
      SetField(typb.mutable_element(), "", ftype->result_type());
    // 4) done

  } else {
    ShouldNotReachHere();
    typb.set_kind(SzlTypeProto::VOID);
  }

  string typestr = "";
  typb.AppendToString(&typestr);
  return typestr;
}


// Backend -> Sawzall

static szl_string NameFor(Proc* proc, string name) {
  return name == "" ? NULL : proc->CopyString(name.c_str());
}


static Field* FieldFor(Proc* proc, FileLine* fl, const SzlFieldProto* fpb) {
  szl_string name = NameFor(proc, fpb->label());
  Field* field = Field::New(proc, fl, name, TypeFor(proc, fl, fpb->type()));
  if (fpb->has_tag()) field->set_tag(fpb->tag());
  return field;
}


static VarDecl* VarDeclFor(Proc* proc, FileLine* fl, const SzlFieldProto* fpb) {
  szl_string name = NameFor(proc, fpb->label());
  return VarDecl::New(proc, fl, name, TypeFor(proc, fl, fpb->type()),
                      NULL, 0, false, NULL);
}


Type* TypeFor(Proc* proc, FileLine* fl, string type_string) {
  SzlTypeProto type;
  type.ParseFromArray(type_string.data(), type_string.size());
  return TypeFor(proc, fl, &type);
}


Type* TypeFor(Proc* proc, FileLine* fl, SzlTypeProto* typb) {
  switch(typb->kind()) {
    case SzlTypeProto::BOOL:
      return SymbolTable::bool_type();
    case SzlTypeProto::BYTES:
      return SymbolTable::bytes_type();
    case SzlTypeProto::FINGERPRINT:
      return SymbolTable::fingerprint_type();
    case SzlTypeProto::FLOAT:
      return SymbolTable::float_type();
    case SzlTypeProto::INT:
      return SymbolTable::int_type();
    case SzlTypeProto::STRING:
      return SymbolTable::string_type();
    case SzlTypeProto::TIME:
      return SymbolTable::time_type();
    case SzlTypeProto::VOID:
      ShouldNotReachHere();
      return SymbolTable::void_type();
    case SzlTypeProto::ARRAY:
      return ArrayType::New(proc, FieldFor(proc, fl, &typb->element()));
    case SzlTypeProto::MAP:
      return MapType::New(proc,
                          FieldFor(proc, fl, &typb->indices(0)),
                          FieldFor(proc, fl, &typb->element()));
    case SzlTypeProto::TUPLE: {
      Scope* scope = Scope::New(proc);
      int tag_count = 0;
      for (int i = 0; i < typb->fields_size(); i++) {
        Field* field = FieldFor(proc, fl, &typb->fields(i));
        if (field->has_tag()) {
          assert(scope->LookupByTag(field->tag()) == NULL);  // tag is available
          assert(field->type()->is_proto());  // type is proto-compatible
          tag_count++;
        }
        scope->Insert(field);
      }
      assert(tag_count == 0 || typb->fields_size() == tag_count);
      return TupleType::New(proc, scope, tag_count > 0, false, false);
    }
    case SzlTypeProto::TABLE: {
      // kind
      TableType* kind = SymbolTable::LookupTableType(typb->table().c_str());
      // param, if any
      Expr* param = NULL;
      szl_int evaluated_param = typb->param();
      if (kind->has_param()) {
        assert(typb->has_param());
        // Note: -1 indicates that param expression required run-time evaluation
        param = Literal::NewInt(proc, fl, NULL, evaluated_param);
      }
      // indices
      List<VarDecl*>* index_decls = List<VarDecl*>::New(proc);
      for (int i = 0; i < typb->indices_size(); i++)
        index_decls->Append(VarDeclFor(proc, fl, &typb->indices(i)));
      // element
      VarDecl* elem = VarDeclFor(proc, fl, &typb->element());
      // weight, if any
      Field* weight = NULL;
      if (kind->has_weight()) {
        assert(typb->has_weight());
        weight = FieldFor(proc, fl, &typb->weight());
      }
      return OutputType::New(proc, kind, param, evaluated_param, index_decls,
                             elem, weight, false, NULL, NULL, true, NULL);
    }
    default:
      ShouldNotReachHere();
  }
  ShouldNotReachHere();
  return NULL;
}


// -----------------------------------------------------------------------------
// Converts protocol buffer encoded string into a Sawzall code string


static string FieldToSpec(const SzlFieldProto& field) {
  string tag = "";
  if (field.has_tag())
    tag = StringPrintf(" @ %d", field.tag());
  string label = "";
  if (field.label().size() > 0)
    label = field.label() + ": ";
  return (label + TypeStringToTypeSpec(field.type()) + tag);
}


string TypeStringToTypeSpec(const string& type_string) {
  SzlTypeProto type;
  if (!type.ParseFromArray(type_string.data(), type_string.size()))
    return "<type string unreadable>";

  switch (type.kind()) {
    case SzlTypeProto::VOID:
      return "void";
    case SzlTypeProto::BOOL:
      return "bool";
    case SzlTypeProto::BYTES:
      return "bytes";
    case SzlTypeProto::FINGERPRINT:
      return "fingerprint";
    case SzlTypeProto::FLOAT:
      return "float";
    case SzlTypeProto::INT:
      return "int";
    case SzlTypeProto::STRING:
      return "string";
    case SzlTypeProto::TIME:
      return "time";
    case SzlTypeProto::ARRAY:
      return "array of " + FieldToSpec(type.element());
    case SzlTypeProto::MAP:
      return
        "map [" + FieldToSpec(type.indices(0)) + "] of " +
        FieldToSpec(type.element());
    case SzlTypeProto::TUPLE:
      { string s;
        for (int i = 0; i < type.fields_size(); i++) {
          if (i > 0)
            s += ", ";
          s += FieldToSpec(type.fields(i));
        }
        return "{ " + s + " }";
      }
    case SzlTypeProto::TABLE:
      { string s = "table " + type.table();
        if (type.has_param() && type.param() >= 0)
          // (-1 is used to indicate no parameter or parameter that requires
          // run-time evaluation)
          s += StringPrintf("(%d)", type.param());
        for (int i = 0; i < type.indices_size(); i++)
          s += "[" + FieldToSpec(type.indices(i)) + "]";
        s += " of " + FieldToSpec(type.element());
        if (type.has_weight())
          s += " weight " + FieldToSpec(type.weight());
        return s;
      }
    case SzlTypeProto::FUNCTION:
      { string s = "function(";
        for (int i = 0; i < type.fields_size(); i++) {
          if (i > 0)
            s += ", ";
          s += FieldToSpec(type.fields(i));
        }
        s += ")";
        if (type.has_element())
          s += ": " + FieldToSpec(type.element());
        return s;
      }
  }

  ShouldNotReachHere();
  return "";  // satisfy C++
}


}  // namespace sawzall
