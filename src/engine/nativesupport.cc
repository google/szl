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
#include <assert.h>
#include <algorithm>

#include "engine/globals.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "utilities/strutils.h"

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
#include "engine/linecount.h"
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/engine.h"
#include "public/sawzall.h"
#include "public/emitterinterface.h"
#include "engine/outputter.h"
#include "engine/compiler.h"
#include "engine/convop.h"
#include "engine/intrinsic.h"
#include "engine/nativesupport.h"


namespace sawzall {

static inline void push(Val**& sp, Val* x) {
  *--sp = x;
}


static inline Val* pop(Val**& sp) {
  return *sp++;
}


static inline Val* top(Val**& sp) {
  return *sp;
}


// push the arguments of va_list onto the interpreter stack
static inline void push_args(Val**& sp, va_list& args, int num_args) {
  // first va_arg is last intrinsic argument , i.e. first to be pushed
  for (int i = 0; i < num_args; ++i) {
    Val* val = va_arg(args, Val*);
    // val->inc_ref();  already done by caller
    push(sp, val);
  }
}


// ----------------------------------------------------------------------------
// The support routines called from generated code are wrapped in a class
// NSupport, which is declared friend of Proc in order to access proc->state_

// TODO there is a lot of room for optimization in these helpers.
// Ref count inc at call site and dec in these helpers could be optimized in
// some cases; passing the dereferenced value instead of the pointer would
// make it possible to skip the inc/dec refs.
// Pushing arguments onto the interpreter stack inline, instead of using a
// va_list would preserve the original evaluation order and therefore result
// in the same undef traps as in interpreted code.
// Generating inlined code instead of calling these helpers would be best.


Val* NSupport::DebugRef(Proc* proc, Val* val) {
  // Compute what reference count will be after dec_ref().
  int32 count = val->ref() - (val->is_ptr() && !val->is_null());
  val->dec_ref();
  return TaggedInts::as_Val(proc, count);
}


Val* NSupport::Uniq(Proc* proc, Val*& var) {
  return (var = var->Uniq(proc));
}


Val* NSupport::CheckAndUniq(Proc* proc, Val*& var) {
  if (var == NULL)
    return NULL;  // error

  return (var = var->Uniq(proc));
}


int NSupport::Inc(Proc* proc, Val*& var) {
  if (var == NULL)
    return 0;  // error

  var = TaggedInts::as_Val(proc, TaggedInts::as_int(var) + 1);
  return 1;  // success
}


int NSupport::Dec(Proc* proc, Val*& var) {
  if (var == NULL)
    return 0;  // error

  var = TaggedInts::as_Val(proc, TaggedInts::as_int(var) - 1);
  return 1;  // success
}


Val* NSupport::AddInt(Proc* proc, IntVal* x, IntVal* y) {
  Val* z = TaggedInts::as_Val(proc, x->val() + y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::SubInt(Proc* proc, IntVal* x, IntVal* y) {
  Val* z = TaggedInts::as_Val(proc, x->val() - y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::MulInt(Proc* proc, IntVal* x, IntVal* y) {
  Val* z = TaggedInts::as_Val(proc, x->val() * y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::DivInt(Proc* proc, IntVal* x, IntVal* y) {
  Val* z;
  if (TaggedInts::is_zero(y)) {
    proc->trap_info_ = proc->PrintError("divide by zero error: %V / 0", proc, x);
    z = NULL;
  } else {
    z = TaggedInts::as_Val(proc, x->val() / y->val());
  }
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::RemInt(Proc* proc, IntVal* x, IntVal* y) {
  Val* z;
  if (TaggedInts::is_zero(y)) {
    proc->trap_info_ = proc->PrintError("divide by zero error: %V %% 0", proc, x);
    z = NULL;
  } else {
    z = TaggedInts::as_Val(proc, x->val() % y->val());
  }
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::ShlInt(Proc* proc, IntVal* x, IntVal* y) {
  Val* z = TaggedInts::as_Val(proc, x->val() << (y->val() & 0x3f));
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::ShrInt(Proc* proc, IntVal* x, IntVal* y) {
  // is a logical shift because x and y are unsigned
  uint64 i = x->val();  // convert to unsigned
  uint64 j = y->val();  // convert to unsigned
  Val* z = TaggedInts::as_Val(proc, i >> (j & 0x3f));
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::AndInt(Proc* proc, IntVal* x, IntVal* y) {
  Val* z = TaggedInts::as_Val(proc, x->val() & y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::OrInt(Proc* proc, IntVal* x, IntVal* y) {
  Val* z = TaggedInts::as_Val(proc, x->val() | y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::XorInt(Proc* proc, IntVal* x, IntVal* y) {
  Val* z = TaggedInts::as_Val(proc, x->val() ^ y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::AddFloat(Proc* proc, FloatVal* x, FloatVal* y) {
  Val* z = Factory::NewFloat(proc, x->val() + y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::SubFloat(Proc* proc, FloatVal* x, FloatVal* y) {
  Val* z = Factory::NewFloat(proc, x->val() - y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::MulFloat(Proc* proc, FloatVal* x, FloatVal* y) {
  Val* z = Factory::NewFloat(proc, x->val() * y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::DivFloat(Proc* proc, FloatVal* x, FloatVal* y) {
  szl_float xval = x->val();
  szl_float yval = y->val();
  x->dec_ref();
  y->dec_ref();
  if (yval == 0.0) {
    proc->trap_info_ = proc->PrintError("divide by zero error: %g / 0.0", xval);
    return NULL;
  }
  return Factory::NewFloat(proc, xval / yval);
}


Val* NSupport::AddFpr(Proc* proc, FingerprintVal* x, FingerprintVal* y) {
  Val* z = Factory::NewFingerprint(proc, FingerprintCat(x->val(), y->val()));
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::AddArray(Proc* proc, ArrayVal* x, ArrayVal* y) {
  assert(x->type()->IsEqual(y->type(), false));
  const int xl = x->length();
  const int yl = y->length();
  ArrayVal* s = x->type()->as_array()->form()->NewVal(proc, xl + yl);
  int i = 0;
  while (i < xl) {
    Val* e = x->at(i);
    e->inc_ref();
    s->at(i++) = e;
  }
  for (int j = 0; j < yl; j++) {
    Val* e = y->at(j);
    e->inc_ref();
    s->at(i++) = e;
  }
  x->dec_ref();
  y->dec_ref();
  return s;
}


Val* NSupport::AddBytes(Proc* proc, BytesVal* x, BytesVal* y) {
  assert(x->is_bytes() && y->is_bytes());
  const int xl = x->length();
  const int yl = y->length();
  BytesVal* s =
    SymbolTable::bytes_form()->NewVal(proc, xl + yl);
  memmove(&(s->u_base()[0]), x->base(), xl * sizeof(unsigned char));
  memmove(&(s->u_base()[xl]), y->base(), yl * sizeof(unsigned char));
  x->dec_ref();
  y->dec_ref();
  return s;
}


Val* NSupport::AddString(Proc* proc, StringVal* x, StringVal* y) {
  assert(x->is_string() && y->is_string());
  const int xl = x->length();
  const int yl = y->length();
  StringVal* s =
    SymbolTable::string_form()->NewVal(proc, xl + yl, x->num_runes() + y->num_runes());
  memmove(&(s->base()[0]), x->base(), xl);
  memmove(&(s->base()[xl]), y->base(), yl);
  x->dec_ref();
  y->dec_ref();
  return s;
}


Val* NSupport::AddTime(Proc* proc, TimeVal* x, TimeVal* y) {
  Val* z = Factory::NewTime(proc, x->val() + y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::SubTime(Proc* proc, TimeVal* x, TimeVal* y) {
  Val* z = Factory::NewTime(proc, x->val() - y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::AddUInt(Proc* proc, UIntVal* x, UIntVal* y) {
  Val* z = Factory::NewUInt(proc, x->val() + y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::SubUInt(Proc* proc, UIntVal* x, UIntVal* y) {
  Val* z = Factory::NewUInt(proc, x->val() - y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::MulUInt(Proc* proc, UIntVal* x, UIntVal* y) {
  Val* z = Factory::NewUInt(proc, x->val() * y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::DivUInt(Proc* proc, UIntVal* x, UIntVal* y) {
  szl_uint xval = x->val();
  szl_uint yval = y->val();
  x->dec_ref();
  y->dec_ref();
  if (yval == 0) {
    proc->trap_info_ = proc->PrintError("divide by zero error: %lld / 0", xval);
    return NULL;
  }
  return Factory::NewUInt(proc, xval / yval);
}


Val* NSupport::ModUInt(Proc* proc, UIntVal* x, UIntVal* y) {
  szl_uint xval = x->val();
  szl_uint yval = y->val();
  x->dec_ref();
  y->dec_ref();
  if (yval == 0) {
    proc->trap_info_ = proc->PrintError("divide by zero error: %lld %% 0", xval);
    return NULL;
  }
  return Factory::NewUInt(proc, xval % yval);
}


Val* NSupport::ShlUInt(Proc* proc, UIntVal* x, UIntVal* y) {
  Val* z = Factory::NewUInt(proc, x->val() << (y->val() & 0x3F));
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::ShrUInt(Proc* proc, UIntVal* x, UIntVal* y) {
  Val* z = Factory::NewUInt(proc, x->val() >> (y->val() & 0x3F));
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::AndUInt(Proc* proc, UIntVal* x, UIntVal* y) {
  Val* z = Factory::NewUInt(proc, x->val() & y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::OrUInt(Proc* proc, UIntVal* x, UIntVal* y) {
  Val* z = Factory::NewUInt(proc, x->val() | y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


Val* NSupport::XorUInt(Proc* proc, UIntVal* x, UIntVal* y) {
  Val* z = Factory::NewUInt(proc, x->val() ^ y->val());
  x->dec_ref();
  y->dec_ref();
  return z;
}


// perform 64-bit int comparison, x and y must have been checked for undef
// returns -1 for x < y, 0 for x == y, and +1 for x > y
// todo we could optimize this routine by either inlining it or
// splitting it in the different flavors (lss, leq, gtr, geq) while still
// returning -1/0/+1 for compatibility with the smi shortcut comparison using
// the native condition code; it may not be worth it, since this code is not
// executed when both operands are smi.
int NSupport::CmpInt(IntVal* x, IntVal* y) {
  szl_int xval = x->val();
  szl_int yval = y->val();
  x->dec_ref();
  y->dec_ref();
  return (xval > yval) - (xval < yval);
}


bool NSupport::EqlFloat(FloatVal* x, FloatVal* y) {
  bool z = x->val() == y->val();
  x->dec_ref();
  y->dec_ref();
  return z;
}


bool NSupport::LssFloat(FloatVal* x, FloatVal* y) {
  bool z = x->val() < y->val();
  x->dec_ref();
  y->dec_ref();
  return z;
}


bool NSupport::LeqFloat(FloatVal* x, FloatVal* y) {
  bool z = x->val() <= y->val();
  x->dec_ref();
  y->dec_ref();
  return z;
}


bool NSupport::EqlBits(Val* x, Val* y) {
  bool z = x->basic64() == y->basic64();
  x->dec_ref();
  y->dec_ref();
  return z;
}


bool NSupport::LssBits(Val* x, Val* y) {
  bool z = x->basic64() < y->basic64();
  x->dec_ref();
  y->dec_ref();
  return z;
}


int NSupport::CmpString(StringVal* x, StringVal* y) {
  const int lx = x->length();
  const int ly = y->length();
  const int cmp = memcmp(x->base(), y->base(), min(lx, ly));
  x->dec_ref();
  y->dec_ref();
  if (cmp != 0)
    return cmp;
  else
    return lx - ly;
}


int NSupport::EqlString(StringVal* x, StringVal* y) {
  const int lx = x->length();
  const int ly = y->length();
  int cmp = 1;   // not equal
  if (lx == ly)
    cmp = memcmp(x->base(), y->base(), lx);

  x->dec_ref();
  y->dec_ref();
  return cmp;
}


int NSupport::CmpBytes(BytesVal* x, BytesVal* y) {
  const int lx = x->length();
  const int ly = y->length();
  const int cmp = memcmp(x->base(), y->base(), min(lx, ly));
  x->dec_ref();
  y->dec_ref();
  if (cmp != 0)
    return cmp;
  return lx - ly;
}


int NSupport::EqlBytes(BytesVal* x, BytesVal* y) {
  const int lx = x->length();
  const int ly = y->length();
  int cmp = 1;   // not equal
  if (lx == ly)
    cmp = memcmp(x->base(), y->base(), lx);

  x->dec_ref();
  y->dec_ref();
  return cmp;
}


bool NSupport::EqlArray(ArrayVal* x, ArrayVal* y) {
  bool z = x->IsEqual(y);
  x->dec_ref();
  y->dec_ref();
  return z;
}


bool NSupport::EqlMap(MapVal* x, MapVal* y) {
  bool z = x->IsEqual(y);
  x->dec_ref();
  y->dec_ref();
  return z;
}


bool NSupport::EqlTuple(TupleVal* x, TupleVal* y) {
  bool z = x->IsEqual(y);
  x->dec_ref();
  y->dec_ref();
  return z;
}


bool NSupport::EqlClosure(ClosureVal* x, ClosureVal* y) {
  bool z = x->IsEqual(y);
  x->dec_ref();
  y->dec_ref();
  return z;
}


void NSupport::FClearB(int i, TupleVal* t) {
  t->dec_ref();
  assert(t->is_unique());
  t->clear_slot_bit_at(i);
}


void NSupport::FSetB(int i, TupleVal* t) {
  t->dec_ref();
  assert(t->is_unique());
  t->set_slot_bit_at(i);
}


bool NSupport::FTestB(int i, TupleVal* t) {
  bool b = t->slot_bit_at(i);
  t->dec_ref();
  return b;
}


// return NULL if index out of range
Val* NSupport::XLoad8(Proc* proc, IntVal* x, BytesVal* b) {
  szl_int i = x->val();
  x->dec_ref();
  if (!b->legal_index(i)) {
    BytesIndexError(proc, b, i);
    b->dec_ref();
    return NULL;
  }
  Val* e = Factory::NewInt(proc, b->at(i));
  b->dec_ref();
  return e;
}


// return NULL if index out of range
Val* NSupport::XLoadR(Proc* proc, IntVal* x, StringVal* s) {
  szl_int i0 = x->val();
  szl_int i = s->byte_offset(proc, i0);
  x->dec_ref();
  if (!s->legal_index(i)) {
    StringIndexError(proc, s, i0);
    s->dec_ref();
    return NULL;
  }
  Val* e = Factory::NewInt(proc, s->at(i));
  s->dec_ref();
  return e;
}


// return NULL if index out of range
Val* NSupport::XLoadV(Proc* proc, IntVal* x, ArrayVal* a) {
  szl_int i = x->val();
  x->dec_ref();
  if (!a->legal_index(i)) {
    ArrayIndexError(proc, a, i);
    a->dec_ref();
    return NULL;
  }
  Val* e = a->at(i);
  e->inc_ref();
  a->dec_ref();
  return e;
}


// return NULL if index out of range
Val* NSupport::XLoadVu(Proc* proc, IntVal* x, ArrayVal* a) {
  szl_int i = x->val();
  x->dec_ref();
  if (!a->legal_index(i)) {
    ArrayIndexError(proc, a, i);
    a->dec_ref();
    return NULL;
  }
  Val* e = Uniq(proc, a->at(i));
  e->inc_ref();
  a->dec_ref();
  return e;
}


// return NULL if not found
Val* NSupport::MLoadV(Proc* proc, MapVal* m, Val* key) {
  int64 index = m->map()->Lookup(key);
  // do not dec ref m yet, since it is used again in the next helper and
  // is only inc ref'd once before calling both helpers
  key->dec_ref();
  if (index < 0) {
    // the next helper will not be called to dec ref m (trap), do it here
    m->dec_ref();
    proc->trap_info_ = "map key was not present";
    return NULL;
  }
  return Factory::NewInt(proc, index);
}


Val* NSupport::MInsertV(Proc* proc, MapVal* m, Val* key) {
  m->dec_ref();
  assert(m->is_unique());
  // inc ref m, since it is used again in the next helper and
  // is only inc ref once before calling both helpers
  m->inc_ref();
  // ref for "key" moved from arg to map
  int32 index = m->map()->InsertKey(key);
  return Factory::NewInt(proc, index);
}


Val* NSupport::MIndexV(MapVal* m, IntVal* index) {
  szl_int i = index->val();
  assert(static_cast<int32>(i) == i);  // generated index, never out of range
  index->dec_ref();
  Val* value = m->map()->Fetch(i);
  value->inc_ref();
  m->dec_ref();
  return value;
}


Val* NSupport::MIndexVu(Proc* proc, MapVal* m, IntVal* index) {
  m->dec_ref();
  assert(m->is_unique());
  szl_int i = index->val();
  assert(static_cast<int32>(i) == i);  // generated index, never out of range
  index->dec_ref();
  Val* value = m->map()->Fetch(i);
  // Uniq will drop its ref count, but it's still in the map, so that is
  // premature.  Therefore, if we do need to unique it, inc_ref it first.
  if (!value->is_unique()) {
    value->inc_ref();
    value = value->Uniq(proc);
    m->map()->SetValue(i, value);  // update array in map
  }
  value->inc_ref();
  return value;
}


void NSupport::MStoreV(MapVal* m, IntVal* index, Val* value) {
  m->dec_ref();
  assert(m->is_unique());
  szl_int i = index->val();
  assert(static_cast<int32>(i) == i);  // generated index, never out of range
  index->dec_ref();
  // ref for "value" moved from arg to map
  m->map()->SetValue(i, value);
}


// return 0 if index out of range
int NSupport::XStore8(Proc* proc, IntVal* x, BytesVal* b, IntVal* e) {
  b->dec_ref();
  assert(b->is_unique());
  szl_int i = x->val();
  x->dec_ref();
  unsigned char c = e->val();  // truncate silently to byte
  e->dec_ref();
  if (!b->legal_index(i)) {
    BytesIndexError(proc, b, i);
    return 0;  // error
  }
  b->at(i) = c;
  return 1;  // success
}


// return 0 if index out of range
int NSupport::XStoreR(Proc* proc, IntVal* x, StringVal* s, IntVal* e) {
  s->dec_ref();
  assert(s->is_unique());
  szl_int i0 = x->val();
  x->dec_ref();
  Rune r = e->val();  // truncate silently to Rune
  e->dec_ref();
  szl_int i = s->byte_offset(proc, i0);
  if (!s->legal_index(i)) {
    StringIndexError(proc, s, i0);
    return 0;  // error
  }
  if (r == 0) {
    proc->trap_info_ = proc->PrintError(
        "character value (%d) is NUL, negative or too large", r);
    return 0;  // error
  }
  s->put(proc, i, r);
  return 1;  // success
}


// return 0 if index out of range
int NSupport::XStoreV(Proc* proc, IntVal* x, ArrayVal* a, Val* e) {
  a->dec_ref();
  assert(a->is_unique());
  szl_int i = x->val();
  x->dec_ref();
  if (!a->legal_index(i)) {
    ArrayIndexError(proc, a, i);
    e->dec_ref();
    return 0;  // error
  }
  Val** elem = &a->at(i);
  assert(*elem != NULL);  // because a must be defined
  (*elem)->dec_ref();
  *elem = e;  // ref goes from stack to array
  return 1;  // success
}


// return 0 if index out of range
int NSupport::XInc8(Proc* proc, int8 delta, IntVal* x, BytesVal* b) {
  b->dec_ref();
  assert(b->is_unique());
  szl_int i = x->val();
  x->dec_ref();
  if (!b->legal_index(i)) {
    BytesIndexError(proc, b, i);
    return 0;  // error
  }
  b->at(i) += delta;
  return 1;  // success
}


// return 0 if index out of range
int NSupport::XIncR(Proc* proc, int8 delta, IntVal* x, StringVal* s) {
  s->dec_ref();
  assert(s->is_unique());
  szl_int i0 = x->val();
  szl_int i = s->byte_offset(proc, i0);
  x->dec_ref();
  if (!s->legal_index(i)) {
    StringIndexError(proc, s, i0);
    return 0;  // error
  }
  s->put(proc, i, s->at(i) + delta);
  return 1;  // success
}


// return 0 if index out of range
int NSupport::XInc64(Proc* proc, int8 delta, IntVal* x, ArrayVal* a) {
  a->dec_ref();
  assert(a->is_unique());
  szl_int i = x->val();
  x->dec_ref();
  if (!a->legal_index(i)) {
    ArrayIndexError(proc, a, i);
    return 0;  // error
  }
  Val** elem = &a->at(i);
  assert(*elem != NULL);  // because a must be defined
  *elem = TaggedInts::as_Val(proc, TaggedInts::as_int(*elem) + delta);
  return 1;  // success
}


void NSupport::MInc64(Proc* proc, int8 delta, MapVal* m, IntVal* index) {
  m->dec_ref();
  assert(m->is_unique());
  szl_int i = index->val();
  assert(static_cast<int32>(i) == i);  // generated index, never out of range
  index->dec_ref();
  m->map()->IncValue(i, delta);
}


Val* NSupport::SLoad8(Proc* proc, IntVal* end, IntVal* beg, BytesVal* b) {
  szl_int i = beg->val();
  beg->dec_ref();
  szl_int j = end->val();
  end->dec_ref();
  b->intersect_slice(&i, &j, b->length());
  return SymbolTable::bytes_form()->NewSlice(proc, b, i, j - i);
  // ref counting managed in NewSlice()
}


Val* NSupport::SLoadR(Proc* proc, IntVal* end, IntVal* beg, StringVal* s) {
  szl_int i = beg->val();
  beg->dec_ref();
  szl_int j = end->val();
  end->dec_ref();
  s->intersect_slice(&i, &j, s->num_runes());
  int num_runes = j - i;
  i = s->byte_offset(proc, i);
  j = s->byte_offset(proc, j);
  return SymbolTable::string_form()->NewSlice(proc, s, i, j - i, num_runes);
  // ref counting managed in NewSlice()
}


Val* NSupport::SLoadV(Proc* proc, IntVal* end, IntVal* beg, ArrayVal* a) {
  szl_int i = beg->val();
  beg->dec_ref();
  szl_int j = end->val();
  end->dec_ref();
  a->intersect_slice(&i, &j, a->length());
  return a->type()->as_array()->form()->NewSlice(proc, a, i, j - i);
  // ref counting managed in NewSlice()
}


void NSupport::SStoreV(Proc* proc, IntVal* end, IntVal* beg, IndexableVal* a, Val* x) {
  a->dec_ref();
  assert(a->is_unique());
  szl_int i = beg->val();
  beg->dec_ref();
  szl_int j = end->val();
  end->dec_ref();
  proc->trap_info_ = Engine::DoSlice(proc, a, i, j, x);
  x->dec_ref();
}


Val* NSupport::NewA(Proc* proc, ArrayType* atype, IntVal* length, Val* init) {
  szl_int len = length->val();
  length->dec_ref();
  ArrayVal* a;
  if (len < 0) {
    proc->trap_info_ = proc->PrintError("negative array length in new(%T): %lld", atype, len);
    a = NULL;
  } else {
    a = atype->form()->NewVal(proc, len);
    for (int i = 0; i < len; i++) {
      a->at(i) = init;
      init->inc_ref();
    }
    init->dec_ref();
  }
  return a;
}


Val* NSupport::NewM(Proc* proc, MapType* mtype, IntVal* occupancy) {
  szl_int occupancy_val = occupancy->val();
  occupancy->dec_ref();
  return mtype->form()->NewValInit(proc, occupancy_val, false);
  // NewValInit never returns NULL
}


Val* NSupport::NewB(Proc* proc, IntVal* length, IntVal* init) {
  szl_int len = length->val();
  length->dec_ref();
  unsigned char ini = init->val();  // truncate silently to byte
  init->dec_ref();
  BytesVal* b;
  if (len < 0) {
    proc->trap_info_ = proc->PrintError("negative length in new(bytes): %lld", len);
    b = NULL;
  } else {
    b = Factory::NewBytes(proc, len);
    memset(b->base(), ini, len);
  }
  return b;
}


Val* NSupport::NewStr(Proc* proc, IntVal* nrunes, IntVal* init) {
  szl_int len = nrunes->val();
  nrunes->dec_ref();
  Rune ini = init->val();  // truncate silently to Rune
  init->dec_ref();
  StringVal* str;
  if (len < 0) {
    proc->trap_info_ = proc->PrintError("negative length in new(string): %lld", len);
    str = NULL;
  } else if (!IsValidUnicode(ini)) {
    proc->trap_info_ = proc->PrintError("illegal unicode character U+%x creating new string", ini);
    str = NULL;
    // other illegal values are left for runtochar to handle, as in RuneStr2Str
  } else {
    char buf[UTFmax];
    int w = runetochar(buf, &ini);
    str = Factory::NewString(proc, len * w, len);
    char* p = str->base();
    for (int i = 0; i < len; i++) {
      memmove(p, buf, w);
      p += w;
    }
  }
  return str;
}


// argument entry is the function entry address relative to the code base
// context is the static link to pass to the function
Val* NSupport::CreateC(Proc* proc, FunctionType* ftype, int entry, Frame* context) {
  return ftype->form()->NewVal(proc, proc->code()->base() + entry, context);
}


// The va_list consists of the byte values in forward order.
Val* NSupport::CreateB(Proc* proc, int num_args, ...) {
  BytesVal* b = Factory::NewBytes(proc, num_args);
  // fill in bytes
  va_list args;
  va_start(args, num_args);
  for (int i = 0; i < num_args; i++) {
    Val* v = va_arg(args, Val*);
    unsigned char x = v->as_int()->val();  // truncate silently to byte
    v->dec_ref();
    b->at(i) = x;
  }
  va_end(args);
  return b;
}


// The va_list consists of the string values in forward order.
Val* NSupport::CreateStr(Proc* proc, int num_args, ...) {
  // build a Rune string
  Rune* buf = new Rune[num_args];
  int64 nbytes = 0;
  va_list args;
  va_start(args, num_args);
  bool valid = true;
  for (int i = 0; i < num_args; i++) {
    Val* v = va_arg(args, Val*);
    szl_int x = v->as_int()->val();
    v->dec_ref();
    if (!IsValidUnicode(x)) {
      proc->trap_info_ = proc->PrintError("illegal unicode character U+%x creating string from array", x);
      valid = false;
      break;
    }
    buf[i] = x;
    nbytes += runelen(x);
  }
  va_end(args);
  if (!valid)
    return NULL;
  // convert into UTF-8 string
  StringVal* s = Factory::NewString(proc, nbytes, num_args);
  RuneStr2Str(s->base(), nbytes, buf, num_args);
  delete[] buf;
  // done
  return s;
}


// Create an array with num_args uninitialized entries, to be
// overwritten by calls to InitA.  Fill the ArrayVal with dummy
// non-pointer values to prevent GC from trying to move them.
Val* NSupport::CreateA(Proc* proc, ArrayType* atype, int num_args) {
  ArrayVal* val = atype->form()->NewVal(proc, num_args);
  Val* zero = TaggedInts::MakeVal(0);
  for (int i = 0; i < num_args; i++)
    val->at(i) = zero;
  return val;
}


// Fill in entries from_val to from_val + num_vals - 1 of array
// created by CreateA.  The va_list consists of the element values in
// forward order followed by the ArrayVal* to fill in.  This strange
// order minimizes the live range of the ArrayVal to help register
// allocation succeed.
Val* NSupport::InitA(Proc* proc, int from_val, int num_vals, ...) {
  va_list args;

  // find the ArrayVal* at the end of the vararg list
  va_start(args, num_vals);
  for (int i = 0; i < num_vals; i++)
    va_arg(args, Val*);
  ArrayVal* aval = va_arg(args, ArrayVal*);
  va_end(args);

  // fill in array elements
  va_start(args, num_vals);
  for (int i = 0; i < num_vals; i++)
    aval->at(from_val + i) = va_arg(args, Val*); // ref goes from stack to array
  va_arg(args, ArrayVal*);
  va_end(args);

  return aval;
}


// Create an empty map with a capacity of num_args key, value pairs.
Val* NSupport::CreateM(Proc* proc, MapType* mtype, int num_args) {
  MapVal* val = mtype->form()->NewValInit(proc, num_args, true);
  return val;
}


// Fill in entries of a map created by CreateM.
// The va_list consists of the element values in forward order followed
// by the MapVal* to fill in.  This strange order minimizes the live
// range of the ArrayVal to help register allocation succeed.
Val* NSupport::InitM(Proc* proc, int num_vals, ...) {
  va_list args;

  // find the MapVal* at the end of the vararg list
  va_start(args, num_vals);
  for (int i = 0; i < num_vals; i++)
    va_arg(args, Val*);
  MapVal* mval = va_arg(args, MapVal*);
  va_end(args);

  // fill in the map entries
  va_start(args, num_vals);
  const int npairs = num_vals/2;
  Map* map = mval->map();
  for (int i = 0; i < npairs; i++) {
    int32 index;
    Val* key = va_arg(args, Val*);  // no dec_ref
    index = map->InsertKey(key);  // ref goes from stack to map
    Val* value = va_arg(args, Val*);  // no dec_ref
    map->SetValue(index, value);  // ref goes from stack to map
  }
  va_arg(args, MapVal*);
  va_end(args);

  return mval;
}


// Create an uninitialized tuple of the given type, to be filled in by
// calls to InitT.  Fill the TupleVal with dummy non-pointer values to
// prevent GC from trying to move them.
Val* NSupport::CreateT(Proc* proc, TupleType* ttype) {
  TupleVal* val = ttype->form()->NewVal(proc, TupleForm::set_inproto);
  Val* zero = TaggedInts::MakeVal(0);
  for (int i = 0; i < ttype->nslots(); i++)
    val->slot_at(i) = zero;
  return val;
}


// Fill in slots from_val to from_val + num_args - 1 of tuple created
// by CreateT.  The va_list consists of the field values in forward
// order followed by the TupleVal* to fill in.  This strange
// order minimizes the live range of the TupleVal to help register
// allocation succeed.
Val* NSupport::InitT(Proc* proc, int from_val, int num_args, ...) {
  va_list args;

  // find the TupleVal* at the end of the vararg list
  va_start(args, num_args);
  for (int i = 0; i < num_args; ++i)
    va_arg(args, Val*);
  TupleVal* tval = va_arg(args, TupleVal*);
  va_end(args);

  // fill in tuple fields
  va_start(args, num_args);
  // first va_arg is first tuple field
  for (int i = 0; i < num_args; ++i)
    // ref goes from stack to tuple
    tval->slot_at(from_val + i) = va_arg(args, Val*);
  va_arg(args, TupleVal*);
  va_end(args);
  return tval;
}


// The va_list consists of the field values in forward order.
void NSupport::CreateTAndStore(Proc* proc, TupleType* ttype,
                               Val* var_index, ...) {
  TupleVal* val = ttype->form()->NewVal(proc, TupleForm::set_inproto);
  // fill in tuple fields
  va_list args;
  va_start(args, var_index);
  const int n = ttype->nslots();
  // first va_arg is first tuple field
  for (int i = 0; i < n; ++i)
    val->slot_at(i) = va_arg(args, Val*);  // ref goes from stack to tuple
  va_end(args);

  const int var_idx = TaggedInts::as_int(var_index);
  var_index->dec_ref();
  Val*& tuple_var = proc->state_.gp_->at(var_idx);
  tuple_var->dec_ref();
  // val->inc_ref();  already incremented in NewVal above
  tuple_var = val;
}


void NSupport::OpenO(Proc* proc, Frame* bp, int var_index, int tab_index,
                     IntVal* tab_param) {
  szl_int param = tab_param->val();
  tab_param->dec_ref();
  bp->at(var_index) = static_cast<IntVal*>(TaggedInts::MakeVal(tab_index));
  Outputter* o = proc->outputter(tab_index);
  OutputType* type = o->type();
  proc->RememberOutputter(o->name(), var_index);
  // for backward-compatibility, detect emitters installed at
  // compile time for tables with unevaluated params
  if (o->emitter() != NULL && !type->is_evaluated_param()) {
    proc->trap_info_ = proc->PrintError(
        "table parameter '%N' must be a constant expression", type->param());
    return;
  }
  // if emitter factory is available
  // install emitters if not installed at compile time
  // (null emitters will be detected at emit time)
  if (proc->emitter_factory() != NULL &&
      type->uses_emitter() && o->emitter() == NULL) {
    if (!type->is_evaluated_param()) {
      type->set_evaluated_param(param);
      if (param < 0) {
        proc->trap_info_ = proc->PrintError(
            "table parameter must be positive; value is '%lld'", param);
        return;
      }
      if (param > kint32max) {  // SzlTypeProto's param is int32
        proc->trap_info_ = proc->PrintError(
            "overflow in table parameter '%lld'", param);
        return;
      }
    }
    string error;
    Emitter* e = proc->emitter_factory()->
        NewEmitter(o->table(), &error);
    if (e == NULL) {
      proc->trap_info_ = proc->PrintError("%s", error.c_str());
      return;
    }
    proc->outputter(tab_index)->set_emitter(e);
  }
}


// The va_list in the following helpers consist of num_args arguments in reverse
// order. The reverse order is required, because va_args must be read in order
// to be pushed on the interpreter stack, thereby reversing the order again.


Val* NSupport::Saw(Proc* proc, void** cache, int num_vars, int num_args, ...) {
  Val** sp0 = proc->state_.sp_;
  Val**& sp = proc->state_.sp_;
  va_list args;
  va_start(args, num_args);
  int* regex_counts = new int[num_vars];  // explicitly deallocated
  Val*** vars = new Val**[num_vars];  // explicitly deallocated
  for (int i = 0; i < num_vars; ++i) {
    regex_counts[i] = va_arg(args, int);
    vars[i] = va_arg(args, Val**);
  }
  StringVal* str = va_arg(args, StringVal*);
  IntVal* count = va_arg(args, IntVal*);
  ArrayVal* a = SymbolTable::array_of_string_type()->form()->NewVal(proc, 0);

  // push format args onto interpreter stack, as expected by intrinsic

  // a->inc_ref();  already done by NewVal
  push(sp, a);

  // str->inc_ref();  already done by caller
  push(sp, str);

  int varn = 0;
  int argn = 2*num_vars + 2;  // regex_count's, var_addr's, str, and count read
  while (varn < num_vars) {
    int regex_count = regex_counts[varn];
    Val** var = vars[varn];
    varn++;
    if (regex_count > 0) {
      // va_list contains regex_count regexes and regex_count flags
      for (int i = 0; i < 2 * regex_count; ++i) {
        Val* val = va_arg(args, Val*);
        // val->inc_ref();  already done by caller
        push(sp, val);
      }
      argn += 2 * regex_count;
      count->inc_ref();
      push(sp, count);
      proc->trap_info_ = Intrinsics::Saw(proc, sp, regex_count, cache);
      if (proc->trap_info_ != NULL) {
        // cleanup interpreter stack
        while (sp < sp0 - 2)  // leave a and str on stack
          pop(sp)->dec_ref();

        // cleanup native args
        while (argn < num_args) {
          va_arg(args, Val*)->dec_ref();
          argn++;
        }
        var = NULL;  // do not assign rest after an error
        break;
      }
    }
    // assign rest if present
    if (var != NULL) {
      Val* new_val = top(sp);
      Val* old_val = *var;
      new_val->inc_ref();
      old_val->dec_ref();
      *var = new_val;
    }
  }
  assert(argn == num_args);
  va_end(args);
  delete[] regex_counts;
  delete[] vars;
  count->dec_ref();
  pop(sp)->dec_ref();  // str

  assert(sp + 1 == sp0);
  return pop(sp);   // array ref count already incremented in Intrinsics::Saw
}


void NSupport::Emit(Proc* proc, int num_args, Val* var, ...) {
  Val** sp0 = proc->state_.sp_;
  Val**& sp = proc->state_.sp_;
  va_list args;
  va_start(args, var);
  push_args(sp, args, num_args);
  va_end(args);
  // call Outputter::Emit
  szl_int out_index = var->as_int()->val();
  proc->trap_info_ = proc->outputter(out_index)->Emit(sp);
  assert(sp == sp0);
}


Val* NSupport::FdPrint(Proc* proc, int fd, int num_args, ...) {
  Val** sp0 = proc->state_.sp_;
  Val**& sp = proc->state_.sp_;
  va_list args;
  va_start(args, num_args);
  push_args(sp, args, num_args - 1);
  StringVal* afmt = va_arg(args, StringVal*);  // last va_arg is format string
  va_end(args);
  Fmt::State f;
  char buf[128];
  F.fmtfdinit(&f, fd, buf, sizeof buf);
  sp = Engine::Print(&f, afmt->base(), afmt->length(), proc, sp);
  assert(sp == sp0);
  afmt->dec_ref();
  F.fmtfdflush(&f);
  // return 0 as result
  return Factory::NewInt(proc, 0);
}


// Handler for traps occuring in generated native code
// This handler sets the execution state in proc to reflect the trapped native
// frame, calls Proc::HandleTrap, which is common to interpreted and native
// code, updates both the pc and sp where execution should continue, and returns
// the current status, which if FAILED, will cause the caller to unwind the
// native stack and abort execution.
Proc::Status NSupport::HandleTrap(char* trap_info, bool fatal, NFrame* fp,
                                  intptr_t sp_adjust, int native_sp_adjust,
                                  Instr*& trap_pc, Val**& trap_sp) {
  Proc* proc = fp->proc_ptr();
  if (fatal)
    proc->set_error();

  proc->trap_info_ = trap_info;
  proc->native_.fp_ = fp;
  proc->native_.sp_ = trap_sp;
  proc->state_.pc_ = trap_pc;

  // fatal traps are not handled by proc->HandleTrap, unwind will be called
  // An intrinsic may have called proc->set_error() to make this trap fatal.
  // Also assertion failure is a special case.
  if (proc->status_ != Proc::FAILED) {
    proc->HandleTrap(sp_adjust, native_sp_adjust, false);
    trap_pc = proc->state_.pc_;  // set continuation pc
    trap_sp = proc->native_.sp_;  // set continuation sp
  } else if (trap_info != NULL &&
             strncmp(trap_info, "assertion failed", 16) == 0) {
    // Assertion failure.  Native code and interpreted code can't handle
    // this the same way because native code unwinds the stack earlier.
    // Therefore we tweak the state here and call PrintStackTrace; later,
    // when Execute is cleaning up, it will call PrintStackTrace again
    // but it has an interlock so it only runs once.
    proc->native_.sp_ += native_sp_adjust;
    proc->trap_pc_ = trap_pc - 1;
    proc->PrintStackTrace();
  }
  return proc->status_;
}

void NSupport::IncCounter(Proc* proc, int n) {
  proc->linecount()->IncCounter(n);
}


// Allocate space for statics on interpreter stack
void NSupport::AllocStatics(Proc* proc, size_t statics_size) {
  int i = statics_size/sizeof(Val*);
  // stack overflow check
  Val** new_sp = proc->state_.sp_ - i;
  if (new_sp < proc->limit_sp()) {
    proc->trap_info_ = proc->PrintString("stack overflow: set --stack_size >= %d",
                                         (proc->initial_sp() - new_sp) * sizeof(Val*));
  } else {
     // zero out variables (because of ref counting during initial
     // assignment to array, map, or tuples)
    while (i-- > 0)
      push(proc->state_.sp_, NULL);

    proc->state_.fp_ = Engine::push_frame(proc->state_.sp_, proc->state_.fp_, NULL, NULL);
    assert(proc->state_.sp_ == proc->state_.fp_->stack());
  }
}


#define TEST_HELPER(name) \
  if (addr == (intptr_t)&name) return #name

// Return the name of a helper given its address, used in debug mode only
// Considering that we invoke objdump for each function to disassemble, it is
// not worthwile to make this function more efficient, e.g. by using a hashtable
const char* NSupport::HelperName(intptr_t addr) {
  TEST_HELPER(DebugRef);
  TEST_HELPER(Uniq);
  TEST_HELPER(CheckAndUniq);
  TEST_HELPER(Inc);
  TEST_HELPER(Dec);
  TEST_HELPER(AddInt);
  TEST_HELPER(SubInt);
  TEST_HELPER(MulInt);
  TEST_HELPER(DivInt);
  TEST_HELPER(RemInt);
  TEST_HELPER(ShlInt);
  TEST_HELPER(ShrInt);
  TEST_HELPER(AndInt);
  TEST_HELPER(OrInt);
  TEST_HELPER(XorInt);
  TEST_HELPER(AddFloat);
  TEST_HELPER(SubFloat);
  TEST_HELPER(MulFloat);
  TEST_HELPER(DivFloat);
  TEST_HELPER(AddFpr);
  TEST_HELPER(AddArray);
  TEST_HELPER(AddBytes);
  TEST_HELPER(AddString);
  TEST_HELPER(AddTime);
  TEST_HELPER(SubTime);
  TEST_HELPER(CmpInt);
  TEST_HELPER(EqlFloat);
  TEST_HELPER(LssFloat);
  TEST_HELPER(LeqFloat);
  TEST_HELPER(EqlBits);
  TEST_HELPER(LssBits);
  TEST_HELPER(CmpString);
  TEST_HELPER(EqlString);
  TEST_HELPER(CmpBytes);
  TEST_HELPER(EqlBytes);
  TEST_HELPER(EqlArray);
  TEST_HELPER(EqlMap);
  TEST_HELPER(EqlTuple);
  TEST_HELPER(FClearB);
  TEST_HELPER(FSetB);
  TEST_HELPER(FTestB);
  TEST_HELPER(XLoad8);
  TEST_HELPER(XLoadR);
  TEST_HELPER(XLoadV);
  TEST_HELPER(XLoadVu);
  TEST_HELPER(MLoadV);
  TEST_HELPER(MInsertV);
  TEST_HELPER(MIndexV);
  TEST_HELPER(MIndexVu);
  TEST_HELPER(MStoreV);
  TEST_HELPER(XStore8);
  TEST_HELPER(XStoreR);
  TEST_HELPER(XStoreV);
  TEST_HELPER(XInc8);
  TEST_HELPER(XIncR);
  TEST_HELPER(XInc64);
  TEST_HELPER(MInc64);
  TEST_HELPER(SLoad8);
  TEST_HELPER(SLoadR);
  TEST_HELPER(SLoadV);
  TEST_HELPER(SStoreV);
  TEST_HELPER(NewA);
  TEST_HELPER(NewM);
  TEST_HELPER(NewB);
  TEST_HELPER(NewStr);
  TEST_HELPER(CreateC);
  TEST_HELPER(CreateB);
  TEST_HELPER(CreateStr);
  TEST_HELPER(CreateA);
  TEST_HELPER(InitA);
  TEST_HELPER(CreateM);
  TEST_HELPER(InitM);
  TEST_HELPER(CreateT);
  TEST_HELPER(InitT);
  TEST_HELPER(CreateTAndStore);
  TEST_HELPER(OpenO);
  TEST_HELPER(Saw);
  TEST_HELPER(Emit);
  TEST_HELPER(FdPrint);
  TEST_HELPER(HandleTrap);
  TEST_HELPER(AllocStatics);
  TEST_HELPER(IncCounter);
  return NULL;
}

#undef TEST_HELPER


// report index error in arrays
void NSupport::ArrayIndexError(Proc* proc, ArrayVal* a, szl_int index) {
  proc->trap_info_ =
    proc->PrintError("index out of bounds (index = %lld, array length = %d)",
                     index, a->length());
}

// report index error in bytes
void NSupport::BytesIndexError(Proc* proc, BytesVal* b, szl_int index) {
  proc->trap_info_ =
    proc->PrintError("index out of bounds (index = %lld, bytes length = %d)",
                     index, b->length());
}

// report index error in strings
void NSupport::StringIndexError(Proc* proc, StringVal* s, szl_int char_index) {
  proc->trap_info_ =
    proc->PrintError("index out of bounds (index = %lld, string length = %d)",
                     char_index, s->num_runes());
}

}  // namespace sawzall
