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

#include "engine/globals.h"
#include "public/logging.h"

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


namespace sawzall {

// ------------------------------------------------------------------------------
// Implementation of Type


bool Type::IsEqual(Type* t, bool test_proto) {
  // types are equal if they are represented by the same Type
  if (t == this)
    return true;

  // types are equal if one of them is a BadType
  // to avoid repeated error messages
  if (is_bad() || t->is_bad())
    return true;

  // types are equal if they say so
  if (IsEqualType(t, test_proto)) {
    assert(t->IsEqualType(this, test_proto));  // verify symmetry
    return true;
  }

  // otherwise they are not equal
  return false;
}


void Type::Initialize() {
  type_name_ = NULL;
  fine_type_ = BOGUSF;
  gross_type_ = BOGUSG;
  enclosing_tuple_ = NULL;
}


// ------------------------------------------------------------------------------
// Implementation of BadType

BadType* BadType::New(Proc* proc) {
  BadType* t = NEW(proc, BadType);
  t->Type::Initialize();
  t->fine_type_ = BAD;
  return t;
}


void BadType::Visit(TypeVisitor *v) {
  v->DoBadType(this);
}


bool BadType::IsEqualType(Type* t, bool test_proto) {
  // this case is handled in Type::IsEqual() to make
  // the implementation easier for IsEqualType()
  // and ensure symmetry
  ShouldNotReachHere();
  return false;
}


// ------------------------------------------------------------------------------
// Implementation of IncompleteType

IncompleteType* IncompleteType::New(Proc* proc) {
  IncompleteType* t = NEW(proc, IncompleteType);
  t->Type::Initialize();
  t->fine_type_ = INCOMPLETE;
  return t;
}


void IncompleteType::Visit(TypeVisitor *v) {
  v->DoIncompleteType(this);
}


bool IncompleteType::IsEqualType(Type* t, bool test_proto) {
  // IncompleteTypes are not equal to any type,
  // not even themselves.
  return false;
}


// ------------------------------------------------------------------------------
// Implementation of BasicType

BasicType* BasicType::New(Proc* proc, Kind kind) {
  BasicType* b = NEW(proc, BasicType);
  b->Type::Initialize();
  b->kind_ = kind;
  switch (kind) {
    case INT:
      b->fine_type_ = Type::INT;
      b->gross_type_ = BASIC64;
      b->form_ = NEW(proc, IntForm);
      break;
    case UINT:
      b->fine_type_ = Type::UINT;
      b->gross_type_ = BASIC64;
      b->form_ = NEW(proc, UIntForm);
      break;
    case BOOL:
      b->fine_type_ = Type::BOOL;
      b->gross_type_ = BASIC64;
      b->form_ = NEW(proc, BoolForm);
      break;
    case FLOAT:
      b->fine_type_ = Type::FLOAT;
      b->gross_type_ = BASIC64;
      b->form_ = NEW(proc, FloatForm);
      break;
    case STRING:
      b->fine_type_ = Type::STRING;
      b->gross_type_ = BASIC;
      b->form_ = NEW(proc, StringForm);
      break;
    case TIME:
      b->fine_type_ = Type::TIME;
      b->gross_type_ = BASIC64;
      b->form_ = NEW(proc, TimeForm);
      break;
    case BYTES:
      b->fine_type_ = Type::BYTES;
      b->gross_type_ = BASIC;
      b->form_ = NEW(proc, BytesForm);
      break;
    case FINGERPRINT:
      b->fine_type_ = Type::FINGERPRINT;
      b->gross_type_ = BASIC64;
      b->form_ = NEW(proc, FingerprintForm);
      break;
    case VOID:
      b->fine_type_ = Type::VOID;
      b->gross_type_ = BASIC;
      b->form_ = NULL;
      break;
  }
  if (b->form_ != NULL)
    b->form_->Initialize(b);
  return b;
}


Type* BasicType::elem_type() const {
  if (is_bytes() || is_string())
    return SymbolTable::int_type();
  else
    return NULL;
}


size_t BasicType::size() const {
  if (is_void())
    return 0;
  return sizeof(Val*);
}


void BasicType::Visit(TypeVisitor *v) {
  v->DoBasicType(this);
}


const char* BasicType::Kind2String(Kind kind) {
  switch (kind) {
    case INT:
      return "int";
    case UINT:
      return "uint";
    case BOOL:
      return "bool";
    case FLOAT:
      return "float";
    case STRING:
      return "string";
    case TIME:
      return "time";
    case BYTES:
      return "bytes";
    case FINGERPRINT:
      return "fingerprint";
    case VOID:
      return "void";
  }
  return NULL;
}


bool BasicType::IsEqualType(Type* t, bool test_proto) {
  return t->is_basic() && t->as_basic()->kind() == kind();
}


// ------------------------------------------------------------------------------
// Implementation of TupleType


void TupleType::AllocateTagMap(Proc* proc) {
  // Allocate a lookup table that maps protocol buffer
  // tags to tuple field indices. Used for fast protocol
  // buffer reading/conversion.
  int min = 0x7FFFFFFF;  // use a constant here - FIX THIS
  int max = 0x80000000;  // use a constant here - THIS THIS
  int length = 0;
  // this 'if' is needed, otherwise the length computation
  // is wrong if there are no fields in the scope - we always
  // want to allocate a map, even if length = 0
  if (!fields_->is_empty()) {
    // determine tag range
    for (int i = 0; i < fields_->length(); i++) {
      Field* field = fields_->at(i);
      assert(field->has_tag());
      int tag = field->tag();
      if (tag < min) min = tag;
      if (tag > max) max = tag;
    }
    assert(min <= max);
    length = max - min + 1;
  }
  // don't create map if the length is too large
  const int max_length = 100000;  // surely large enough
  if (length <= max_length) {
    // create a map
    List<int>* map = List<int>::New(proc);
    // for now just fill it with -1's (should really do
    // this in the List constructor eventually)
    for (int i = 0; i < length; i++)
      map->Append(-1);
    assert(map->length() == length);
    // now set in the map entries for known tags
    for (int i = 0; i < fields_->length(); i++)
      (*map)[fields_->at(i)->tag() - min] = i;
    // done
    min_tag_ = min;
    map_ = map;
  }
  // else: map would be too large, so we don't create it
  // => is_proto() will return false (see also comment
  // for TupleType constructor (.h file).
}


void TupleType::AllocateDefaultProto(Proc* proc) {
  if (!is_proto() || default_proto_val_ != NULL)
    return;

  assert(proc != NULL);
  assert(default_proto_val_ == NULL);
  const char* error = protocolbuffers::DefaultTuple(proc, &default_proto_val_,
                                                    this, true);
  CHECK(error == NULL) << ": " << error;
}


TupleType* TupleType::New(Proc* proc, Scope* scope, bool is_proto,
                          bool is_message, bool is_predefined) {
  TupleType* t = NewUnfinished(proc, scope, NULL, NULL);
  return t->Finish(proc, is_proto, is_message, is_predefined);
}


TupleType* TupleType::NewUnfinished(Proc* proc, Scope* scope, TypeName* tname,
                                    TupleType* enclosing_tuple) {
  assert(scope != NULL);

  TupleType* t = NEW(proc, TupleType);
  t->Type::Initialize();
  t->fine_type_ = TUPLE;
  t->is_finished_ = false;
  t->is_predefined_ = false;
  t->scope_ = scope;
  t->fields_ = List<Field*>::New(proc);
  t->is_message_ = false;  // may change when complete
  t->is_auto_proto_ = false;
  t->min_tag_ = 0;
  t->map_ = NULL;
  t->default_proto_val_ = NULL;
  t->nslots_ = -1;  // slots have not been assigned yet
  t->ntotal_ = -1;
  t->fields_read_ = NONE;
  t->tested_for_equality_ = false;
  t->enclosing_tuple_ = enclosing_tuple;
  if (tname != NULL) {
    t->set_type_name(tname);
    tname->set_type(t);
  }

  t->form_ = NEW(proc, TupleForm);
  t->form_->Initialize(t);
  return t;
}


TupleType* TupleType::Finish(Proc* proc, bool is_proto, bool is_message,
                             bool is_predefined) {
  is_message_ = is_message;

  // create the field list
  // (the fields may not be scanned before the tuple is finished)
  for (int i = 0; i < scope_->num_entries(); i++) {
    Field* field = scope_->entry_at(i)->AsField();
    if (field != NULL)
      fields_->Append(field);
  }

  if (is_proto) {
    // this assumes we are not adding/changing any fields in the scope
    // which is true at the moment and probably should always be true
    // may want to add some assertion checking in debug mode (e.g.
    // check that the number of scope entries hasn't changed)
    // We can build the map before assigning slots because the entries are
    // indices in the scope, not the slot array.
    AllocateTagMap(proc);
  }

  // Register this tuple type with the Proc under which it was created.
  // If necessary, we will bind its fields and allocate its default proto
  // after parsing and before code generation, when we know what fields
  // were referenced.
  proc->RegisterTupleType(this);
  is_finished_ = true;
  is_predefined_ = is_predefined;

  if (is_predefined) {
    // For predefined tuple types bind fields and allocate default proto now
    // because define_tuple() builds a table of slot numbers at initialization.
    SetAllFieldsRead(true);
    BindFieldsToSlots(proc);
    AllocateDefaultProto(proc);
  }
  return this;
}


void TupleType::BindFieldsToSlots(Proc* proc) {
  if (fields_bound())
    return;

  // Assign slot indices to all read tuple fields
  int index = 0;
  int field_count = 0;  // count all fields when limiting tuple size
  for (int i = 0; i < fields_->length(); i++) {
    Field* field = fields_->at(i);
    field_count++;
    if (field->read()) {
      assert(field->type()->size() == sizeof(Val*));
      field->set_slot_index(index);
      index++;
    }
  }
  nslots_ = index;

  // If the tuple is a proto tuple, it may contain optional fields. The presence
  // information for a field is stored in the tuple object in form of a bit
  // vector following the tuple fields. Since the number of fields is usually
  // small (<= 32), for simplicity the bit vector is present for all tuples and
  // not just proto tuples. This also simplifies conversions between plain
  // and proto tuples. Thus, the tuple size in slots is extended by the number
  // of slots needed to represent that bit vector.
  //
  // the number of extra slots required for the presence bit vector
  const int nbits = sizeof(Val*) * 8;  // bits per Val*
  assert(Align(0, nbits) / nbits == 0);  // empty tuples must get no extra bits!
  const int nextra = Align(nslots_, nbits) / nbits;
  // the total length of the tuple in slots
  ntotal_ = nslots_ + nextra;
}


void TupleType::BindFieldsToSlotsForAll(Proc* proc) {
  // Assignment of slots is deferred until we know which fields are referenced.
  proc->ApplyToAllTupleTypes(&TupleType::BindFieldsToSlots);

  // Allocation of default proto values is deferred until we know which
  // fields are referenced and so have slots assigned to them.
  if (FLAGS_preallocate_default_proto)
    proc->ApplyToAllTupleTypes(&TupleType::AllocateDefaultProto);
}


void TupleType::SetAllFieldsRead(bool recurse) {
  // We don't track writes to fields, but we could do that by duplicating
  // the logic for field reads.
  FieldsRead new_fields_read = recurse ? ALL_NESTED : ALL;

  if (new_fields_read > fields_read_) {
    CHECK(is_finished());    // must be called after all fields are known
    CHECK(!fields_bound());  // must be called before the fields are bound
    // set fields_read_ early to prevent infinite recursion
    fields_read_ = new_fields_read;
    for (int i = 0; i < fields_->length(); i++) {
      Field* f = fields_->at(i);
      f->set_read();
      if (recurse) {
        f->type()->SetAllFieldsRead(recurse);
      }
    }
  }
}


void TupleType::ClearAllFieldsRead() {
  if (is_predefined_ || tested_for_equality_) {
    // These tuples must have their fields set to referenced.
    return;
  }
  fields_read_ = NONE;
  CHECK(is_finished());    // must be called after all fields are known
  CHECK(!fields_bound());  // must be called before the fields are bound
  for (int i = 0; i < fields_->length(); i++) {
    Field* f = fields_->at(i);
    f->clear_read();
  }
}


Field* Type::MakeProtoField(Proc* proc, Field* f, ProtoForward* forward) {
  assert(f != NULL);
  Type* t = f->type()->MakeProto(proc, forward);
  if (! t->is_proto())
    return f;  // couldn't convert field type into proto type
  return Field::New(proc, f->file_line(), f->name(), t);
}


Type* TupleType::MakeProto(Proc* proc, ProtoForward* forward) {
  // If it is already a proto, just use it.
  if (is_proto())
    return this;
  // If there is a proto being made for this (recursive ref), just use it.
  for (ProtoForward* p = forward; p != NULL; p = p->parent) {
    if (p->type == this)
      return p->proto;
  }
  Scope* s = Scope::New(proc);
  TupleType* proto = NewUnfinished(proc, s, NULL, enclosing_tuple());
  ProtoForward our_forward = { this, proto, forward };
  int tag = 0;
  // Note that we use the scope, not the field list,
  // so that we get types and static decls into the proto scope as well.
  Scope* s0 = scope();
  for (int i = 0; i < s0->num_entries(); i++) {
    Object* obj = s0->entry_at(i);
    Field* f = obj->AsField();
    if (f != NULL) {
      // field; make its type a suitable proto type
      f = MakeProtoField(proc, f, &our_forward);
      if (f == NULL || !f->type()->is_proto())
        return this;  // couldn't convert field type into proto type
      f->set_tag(++tag);
      s->InsertOrDie(f);
    } else {
      // static variable or type name
      assert(obj->AsVarDecl() != NULL || obj->AsTypeName() != NULL);
      s->InsertOrDie(obj);
    }
  }
  TupleType* t = proto->Finish(proc, true, true, false);
  t->is_auto_proto_ = true;
  assert(t->is_proto());
  return t;
}


int TupleType::inproto_index(Field* f) const {
  CHECK(fields_bound());
  return nslots() * sizeof(Val*) * 8 /* bits for all the tuple slots */ +
         f->slot_index();
}


bool TupleType::IsFullyNamed() const {
  for (const TupleType* t = this; t != NULL; t = t->enclosing_tuple())
    if (t->type_name() == NULL)
      return false;
  return true;
}


void TupleType::Visit(TypeVisitor *v) {
  v->DoTupleType(this);
}


void TupleType::VisitChildren(TypeVisitor *v) {
  // Visit the types and static decls as well as the fields.
  for (int i = 0; i < scope()->num_entries(); i++) {
    Object* obj = scope()->entry_at(i);
    if (obj->AsField() != NULL) {
      if (!obj->AsField()->recursive())
        obj->type()->Visit(v);
    } else if (obj->AsVarDecl() != NULL) {
      obj->type()->Visit(v);
    }
  }
}


Field* TupleType::field_for(int tag) {
  assert(is_proto());
  int i = tag - min_tag_;
  // be careful and check if the index is valid (we
  // may use the wrong protocol buffer and we
  // must not crash)
  if (!map_->valid_index(i)) {
    return NULL;
  } else {
    int index = map_->at(i);
    if (index < 0)
      return NULL;  // just an unrecognized tag, may be OK
    else
      return fields_->at(index);
  }
}


bool TupleType::IsEqualType(Type* t, bool test_proto) {
  if (is_finished() && t->is_tuple() && t->as_tuple()->is_finished()) {
    if (test_proto) {
      if (is_proto() != t->is_proto())
        return false;  // both must have same proto-ness
      if (is_message() != t->as_tuple()->is_message())
        return false;  // both must have same message-ness
    }
    // Ignore local types and static decls
    List<Field*>* x = fields();
    List<Field*>* y = t->as_tuple()->fields();
    // tuples must match field by field
    if (x->length() == y->length()) {
      for (int i = 0; i < x->length(); i++) {
        Field* vx = x->at(i);
        Field* vy = y->at(i);
        assert(vx != NULL && vy != NULL);
        // both fields must have names, or both must have no names
        if (vx->is_anonymous() != vy->is_anonymous())
          return false;
        // if they have names, they must match
        if (!vx->is_anonymous() && strcmp(vx->name(), vy->name()) != 0)
          return false;
        // if test_proto is checked, tags must match if there are any
        if (test_proto && vx->has_tag()) {
          assert(vy->has_tag());  // since proto-ness is the same
          if (vx->tag() != vy->tag())
            return false;
        }
        // field types must match
        if (!vx->type()->IsEqual(vy->type(), test_proto))
          return false;
      }
      // all fields match
      // force all fields read - read() becomes unreliable
      assert(this != t);  // we assume this case already handled by caller
      tested_for_equality_ = true;
      t->as_tuple()->set_tested_for_equality();
      return true;
    }
  }
  return false;
}


// ------------------------------------------------------------------------------
// Implementation of ArrayType

ArrayType* ArrayType::New(Proc* proc, Field* elem) {
  ArrayType* t = NewUnfinished(proc, NULL, NULL);
  return t->Finish(proc, elem);
}

ArrayType* ArrayType::NewUnfinished(Proc* proc, TypeName* tname,
                                    TupleType* enclosing_tuple) {
  ArrayType* t = NEW(proc, ArrayType);
  t->Type::Initialize();
  t->is_finished_ = false;
  t->stop_recursion_ = false;
  t->elem_ = SymbolTable::incomplete_field();
  t->fine_type_ = ARRAY;
  t->enclosing_tuple_ = enclosing_tuple;
  if (tname != NULL) {
    t->set_type_name(tname);
    tname->set_type(t);
  }
  t->form_ = NEW(proc, ArrayForm);
  t->form_->Initialize(t);
  return t;
}


ArrayType* ArrayType::Finish(Proc* proc, Field* elem) {
  assert(elem != NULL && !elem->has_tag());
  elem_ = elem;
  is_finished_ = true;
  return this;
}


Type* ArrayType::MakeProto(Proc* proc, ProtoForward* forward) {
  // If it is already a proto, just use it.
  if (is_proto())
    return this;
  // If there is a proto being made for this (recursive ref), just use it.
  for (ProtoForward* p = forward; p != NULL; p = p->parent)
    if (p->type == this)
      return p->proto;
  ArrayType* proto = NewUnfinished(proc, NULL, NULL);
  ProtoForward our_forward = { this, proto, forward };
  Field* f = MakeProtoField(proc, elem(), &our_forward);
  if (! f->type()->is_proto())
    return this;  // couldn't convert field type into proto type
  return proto->Finish(proc, f);
}


void ArrayType::set_tested_for_equality() {
  if (!stop_recursion_) {
    stop_recursion_ = true;
    elem_->type()->set_tested_for_equality();
    stop_recursion_ = false;
  }
}


void ArrayType::SetAllFieldsRead(bool recurse) {
  if (!stop_recursion_) {
    stop_recursion_ = true;
    elem_->type()->SetAllFieldsRead(recurse);
    stop_recursion_ = false;
  }
}


void ArrayType::ClearAllFieldsRead() {
  if (!stop_recursion_) {
    stop_recursion_ = true;
    elem_->type()->ClearAllFieldsRead();
    stop_recursion_ = false;
  }
}


const char* ArrayType::elem_name() const {
  return elem_->name();
}


Type* ArrayType::elem_type() const {
  return elem_->type();
}


void ArrayType::Visit(TypeVisitor *v) {
  v->DoArrayType(this);
}


void ArrayType::VisitChildren(TypeVisitor *v) {
  if (!elem()->recursive())
    elem_type()->Visit(v);
}


bool ArrayType::IsEqualType(Type* t, bool test_proto) {
  ArrayType* a = t->as_array();
  if (a != NULL) {
    // element types must match
    return elem_type()->IsEqual(a->elem_type(), test_proto);
  }
  return false;
}


// ------------------------------------------------------------------------------
// Implementation of TableType

TableType* TableType::New(Proc* proc, szl_string name, bool has_param, bool has_weight) {
  TableType* t = NEW(proc, TableType);
  t->name_ = name;
  t->has_param_ = has_param;
  t->has_weight_ = has_weight;
  assert(name != NULL);
  return t;
}


// ------------------------------------------------------------------------------
// Implementation of OutputType

OutputType* OutputType::New(
    Proc* proc,
    TableType* kind,
    Expr* param,
    int evaluated_param,
    List<VarDecl*>* index_decls,
    VarDecl* elem_decl,
    Field* weight,
    bool is_proc, List<Expr*>* index_format_args,
    List<Expr*>* elem_format_args,
    bool is_static,
    TupleType* enclosing_tuple
) {
  OutputType* t = NEW(proc, OutputType);
  t->Type::Initialize();
  t->kind_ = kind;
  t->param_ = param;
  t->evaluated_param_ = evaluated_param;
  t->index_decls_ = index_decls;
  t->elem_decl_ = elem_decl;
  t->weight_ = weight;
  t->is_proc_ = is_proc;
  t->index_format_args_ = index_format_args;
  t->elem_format_args_ = elem_format_args;
  t->is_static_ = is_static;
  t->enclosing_tuple_ = enclosing_tuple;
  assert(kind->has_param() == (param != NULL));
  assert(kind->has_weight() == (weight != NULL));
  t->fine_type_ = OUTPUT;
  return t;
}


Type* OutputType::elem_type() const {
  return elem_decl_->type();
}


void OutputType::Visit(TypeVisitor *v) {
  v->DoOutputType(this);
}


void OutputType::VisitChildren(TypeVisitor *v) {
  // Some of these types are restricted to simple types which there
  // is little value in visiting, but we visit anyway for completeness.
  if (param() != NULL)
    param()->type()->Visit(v);
  for (int i = 0; i < index_decls_->length(); i++)
    index_decls_->at(i)->type()->Visit(v);
  elem_decl_->type()->Visit(v);
  if (weight() != NULL)
    weight()->type()->Visit(v);
  if (index_format_args() != NULL)
    for (int i = 0; i < index_format_args()->length(); i++)
      index_format_args()->at(i)->type()->Visit(v);
  if (elem_format_args() != NULL)
    for (int i = 0; i < elem_format_args()->length(); i++)
      elem_format_args()->at(i)->type()->Visit(v);
}


bool OutputType::IsEqualType(Type* t, bool test_proto) {
  OutputType* o = t->as_output();
  if (o == NULL)
    return false;
  // kinds must match (required)
  if (o->kind() != kind())
    return false;
  // parameter values must match (optional)
  if (!((o->param() == NULL && param() == NULL) ||
        (o->param() != NULL && param() != NULL &&
         o->param()->as_int()->IsEqual(param()->as_int()))) )
    return false;
  // index variables must have same types and names
  if (o->index_decls()->length() != index_decls()->length())
    return false;
  for (int i = 0; i < index_decls()->length(); i++) {
    VarDecl* oiv = o->index_decls()->at(i);
    VarDecl* iv = index_decls()->at(i);
    if (!oiv->type()->IsEqual(iv->type(), true))
      return false;
    // TODO: remove name requirement - artifact of old implementation
    szl_string oiname = oiv->name();
    szl_string iname = iv->name();
    if (!((oiname == NULL && iname == NULL) ||
          (oiname != NULL && iname != NULL && strcmp(oiname, iname) == 0)))
      return false;
  }
  // element variables must have the same types and names
  // TODO: remove name requirement - artifact of old implementation
  if (!o->elem_type()->IsEqual(elem_type(), true))
    return false;
  szl_string oename = o->elem_decl()->name();
  szl_string ename = elem_decl()->name();
  if (!((oename == NULL && ename == NULL) ||
        (oename != NULL && ename != NULL && strcmp(oename, ename) == 0)))
    return false;
  // weight types must match (optional)
  if (!((o->weight() == NULL && weight() == NULL) ||
        (o->weight() != NULL && weight() != NULL &&
         o->weight()->type()->IsEqual(weight()->type(), true))))
    return false;
  // index proc/file formats must match (optional)
  if (o->index_format_args() != NULL && index_format_args() != NULL &&
      o->index_format_args()->length() == index_format_args()->length()) {
    for (int i = 0; i < index_format_args()->length(); i++) {
      Expr* e = index_format_args()->at(i);
      Expr* oe = o->index_format_args()->at(i);
      assert(e != NULL && oe != NULL);
      if (!e->type()->IsEqual(oe->type(), true))
        return false;
    }
  } else if (!(o->index_format_args() == NULL && index_format_args() == NULL)) {
    return false;
  }
  // element formats must match (optional)
  if (o->elem_format_args() != NULL && elem_format_args() != NULL &&
      o->elem_format_args()->length() == elem_format_args()->length()) {
    for (int i = 0; i < elem_format_args()->length(); i++) {
      Expr* e = elem_format_args()->at(i);
      Expr* oe = o->elem_format_args()->at(i);
      assert(e != NULL && oe != NULL);
      if (!e->type()->IsEqual(oe->type(), true))
        return false;
    }
  } else if (!(o->elem_format_args() == NULL && elem_format_args() == NULL)) {
    return false;
  }
  return true;
}


// ------------------------------------------------------------------------------
// Implementation of FunctionType

FunctionType* FunctionType::New(Proc* proc) {
  // Used only during initialization to create types used by intrinsics and
  // extensions.  Note that in this case the type is marked "finished"
  // immediately yet the initialization may subsequently call par(), opt() and
  // res(), changing the type.  This should be harmless during initialization.
  assert(proc == Proc::initial_proc());
  FunctionType* t = NewUnfinished(proc, NULL, NULL);
  t->is_predefined_ = true;
  return t->Finish(proc);
}


FunctionType* FunctionType::NewUnfinished(Proc* proc, TypeName* tname,
                                          TupleType* enclosing_tuple) {
  FunctionType* f = NEWP(proc, FunctionType);
  f->Type::Initialize();
  f->is_finished_ = false;
  f->is_predefined_ = false;      // tentative, see FunctionType::New()
  f->result_ = SymbolTable::void_field();
  f->gross_type_ = BASIC64;
  f->fine_type_ = FUNCTION;
  f->enclosing_tuple_ = enclosing_tuple;
  if (tname != NULL) {
    f->set_type_name(tname);
    tname->set_type(f);
  }
  f->form_ = NEW(proc, ClosureForm);
  f->form_->Initialize(f);
  return f;
}


FunctionType* FunctionType::Finish(Proc* proc) {
  is_finished_ = true;
  return this;
}


Type* FunctionType::result_type() const {
  return result_->type();
}


void FunctionType::add_parameter(Field* field) {
  assert(parameters_.length() == 0 || !parameters_.last()->has_value() ||
         field->has_value());  // only optionals allowed after first optional
  parameters_.Append(field);
}


FunctionType* FunctionType::par(szl_string name, Type* type) {
  assert(is_predefined_);
  add_parameter(Field::New(Proc::initial_proc(), SymbolTable::init_file_line(),
                           name, type));
  return this;
}


FunctionType* FunctionType::par(Type* type) {
  assert(type != NULL);
  return par(NULL, type);
}


FunctionType* FunctionType::opt(Expr* value) {
  assert(is_predefined_);
  Field* field = Field::New(Proc::initial_proc(), SymbolTable::init_file_line(),
                            NULL, value->type());
  field->set_value(value);
  add_parameter(field);
  return this;
}


FunctionType* FunctionType::res(Type* type) {
  assert(is_predefined_);
  assert(type != NULL);
  result_ = Field::New(Proc::initial_proc(), SymbolTable::init_file_line(),
                       NULL, type);
  return this;
}


void FunctionType::Visit(TypeVisitor *v) {
  v->DoFunctionType(this);
}


void FunctionType::VisitChildren(TypeVisitor *v) {
  // If default values for non-intrinsics are added, they should probably
  // be reached through the Function object, not the FunctionType object.
  // We ignore them here, if present.
  for (int i = 0; i < parameters()->length(); i++) {
    Field* param = parameters()->at(i);
    if (!param->recursive())
      param->type()->Visit(v);
  }
  if (has_result() && !result()->recursive())
    result_type()->Visit(v);
}


bool FunctionType::IsEqualParameters(FunctionType* f, bool test_proto) {
  List<Field*>* x = parameters();
  List<Field*>* y = f->parameters();
  // signatures must match parameter by parameter
  if (x->length() == y->length()) {
    for (int i = 0; i < x->length(); i++) {
      Type* px = x->at(i)->type();
      Type* py = y->at(i)->type();
      assert(px != NULL && py != NULL);
      // parameter types must match
      if (!px->IsEqual(py, test_proto))
        return false;
      // we don't allow optionals for now
      if (x->at(i)->has_value() || y->at(i)->has_value())
        return false;
    }
    // parameters are equal
    return true;
  }
  return false;
}


bool FunctionType::IsEqualType(Type* t, bool test_proto) {
  FunctionType* f = t->as_function();
  if (f != NULL) {
    // Parameters must match
    if (!IsEqualParameters(f, test_proto))
      return false;

    // result types must match
    if (!result_type()->IsEqual(f->result_type(), test_proto))
        return false;

    // types are equal
    return true;
  }
  return false;
}


// ------------------------------------------------------------------------------
// Implementation of MapType

MapType* MapType::New(Proc* proc, Field* index, Field* elem) {
  MapType* t = NewUnfinished(proc, NULL, NULL);
  return t->Finish(proc, index, elem);
}


MapType* MapType::NewUnfinished(Proc* proc, TypeName* tname,
                                TupleType* enclosing_tuple) {
  MapType* t = NEW(proc, MapType);
  t->Type::Initialize();
  t->is_finished_ = false;
  t->stop_recursion_ = false;
  t->index_ = SymbolTable::incomplete_field();
  t->elem_ = SymbolTable::incomplete_field();
  t->fine_type_ = MAP;
  t->key_array_type_ = SymbolTable::array_of_incomplete_type();
  t->enclosing_tuple_ = enclosing_tuple;
  if (tname != NULL) {
    t->set_type_name(tname);
    tname->set_type(t);
  }
  t->form_ = NEW(proc, MapForm);
  t->form_->Initialize(t);
  return t;
}


MapType* MapType::Finish(Proc* proc, Field* index, Field* elem) {
  assert(index != NULL);
  assert(elem != NULL);
  index_ = index;
  elem_ = elem;
  key_array_type_ = ArrayType::New(proc, Field::New(proc, index->file_line(),
                                                    NULL, index->type()));
  // maps call IsEqual and Hash on their index values.
  index->type()->set_tested_for_equality();
  is_finished_ = true;
  return this;
}


void MapType::set_tested_for_equality() {
  if (!stop_recursion_) {
    stop_recursion_ = true;
    elem_->type()->set_tested_for_equality();
    stop_recursion_ = false;
  }
}


void MapType::SetAllFieldsRead(bool recurse) {
  if (!stop_recursion_) {
    stop_recursion_ = true;
    index_->type()->SetAllFieldsRead(recurse);
    elem_->type()->SetAllFieldsRead(recurse);
    stop_recursion_ = false;
  }
}


void MapType::ClearAllFieldsRead() {
  if (!stop_recursion_) {
    stop_recursion_ = true;
    index_->type()->ClearAllFieldsRead();
    elem_->type()->ClearAllFieldsRead();
    stop_recursion_ = false;
  }
}


const char* MapType::index_name() const  {
  return index_->name();
}


Type* MapType::index_type() const {
  return index_->type();
}


const char* MapType::elem_name() const {
  return elem_->name();
}


Type* MapType::elem_type() const {
  return elem_->type();
}


void MapType::Visit(TypeVisitor *v) {
  v->DoMapType(this);
}


void MapType::VisitChildren(TypeVisitor *v) {
  if (!index()->recursive())
    key_array_type()->Visit(v);
  if (!elem()->recursive())
    elem_type()->Visit(v);
}


bool MapType::IsEqualType(Type* t, bool test_proto) {
  MapType* m = t->as_map();
  if (m != NULL) {
    // index & element types must match
    return
      index_type()->IsEqual(m->index_type(), test_proto) &&
      elem_type()->IsEqual(m->elem_type(), test_proto);
  }
  return false;
}

} // namespace sawzall

