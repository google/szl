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

// Sawzall extension counting the number of fields in a proto tuple that have
// the inproto bit set (recursively, if needed).

#include <assert.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/frame.h"
#include "engine/proc.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/engine.h"
#include "engine/intrinsic.h"


namespace sawzall {


static void RecurseIntoArray(ArrayVal* aval, int* count);  // Forward decl


// Looks at all fields of the tuple tval and increases *count for every inproto
// field found. Recurses into tuples and arrays if it finds them.
static void RecurseIntoTuple(TupleVal* tval, int* count) {
  TupleType* ttype = tval->type()->as_tuple();
  List<Field*>* fields = ttype->fields();
  for (int i = 0; i < fields->length(); ++i) {
    Field* field = fields->at(i);
    if (field->recursive() ||
        !field->read() ||
        !tval->field_bit_at(ttype, field)) {
      continue;
    }
    ++*count;
    Val* val = tval->field_at(field);
    if (val->is_tuple()) {
      RecurseIntoTuple(val->as_tuple(), count);
    } else if (val->is_array()) {
      RecurseIntoArray(val->as_array(), count);
    }
  }
}


// If the array's elements are tuples or arrays themselves, recurses into each
// of these values. Simply returns otherwise. count doesn't get incremented in
// this function since inproto() can't be used on array elements.
static void RecurseIntoArray(ArrayVal* aval, int* count) {
  Field* field = aval->type()->as_array()->elem();
  Type* type = field->type();
  if (field->recursive() || !(type->is_array() || type->is_tuple()))
    return;
  for (int i = 0; i < aval->semantic_length(); ++i) {
    Val* val = aval->at(i);
    if (val->is_tuple()) {
      RecurseIntoTuple(val->as_tuple(), count);
    } else if (val->is_array()) {
      RecurseIntoArray(val->as_array(), count);
    }
  }
}


// Documentation string for the inprotocount() intrinsic.
static const char* inprotocount_doc =
    "Returns the number of fields in a proto tuple that have the inproto bit "
    "set. Fields in nested tuples are taken into account. In the case of an "
    "array of nested tuples, the fields in each tuple get counted.";

// Interface function to Sawzall space. Pops a tuple from the stack and returns
// an int count.
static const char* inprotocount(Proc* proc, Val**& sp) {
  static const char* non_proto_error =
      "inprotocount: can only be called with proto tuples";
  Val* val = Engine::pop(sp);
  assert(val->is_tuple());
  if (!val->type()->is_proto()) {
    val->dec_ref();
    return proc->PrintError(non_proto_error);
  }

  int count = 0;
  RecurseIntoTuple(val->as_tuple(), &count);
  val->dec_ref();

  Engine::push_szl_int(sp, proc, count);
  return NULL;  // success
}

// Registers the intrinsic.
static void Initialize() {
  assert(SymbolTable::is_initialized());
  Proc* proc = Proc::initial_proc();
  FunctionType* func_type = FunctionType::New(proc)->
      par("t", SymbolTable::any_tuple_type())->
      res(SymbolTable::int_type());
  SymbolTable::RegisterIntrinsic(
      "inprotocount", func_type, inprotocount, inprotocount_doc,
      Intrinsic::kNormal);
}

}  // namespace sawzall


// Note: This must happen outside the namespace
// to avoid linker/name mangling problems.
REGISTER_MODULE_INITIALIZER(
  SawzallExtensionInprotoCount,
  { REQUIRE_MODULE_INITIALIZED(Sawzall);
    sawzall::Initialize();
  }
);
