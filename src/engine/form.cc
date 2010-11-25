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
#include "public/hashutils.h"

#include "utilities/strutils.h"
#include "utilities/timeutils.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/proc.h"
#include "engine/code.h"


namespace sawzall {

// ----------------------------------------------------------------------------
// Cmp helper.

template <class T>
static Val* cmp(T a, T b) {
if (a < b)
    return TaggedInts::MakeVal(-1);
  else if (a > b)
    return TaggedInts::MakeVal(1);
  else
    return TaggedInts::MakeVal(0);
}


// ----------------------------------------------------------------------------
// Fingerprint helper

const szl_fingerprint kFingerSeed() {
  return ::Fingerprint(0);
}


// ----------------------------------------------------------------------------
// Implementation of Form (default versions)

bool Form::IsUnique(const Val* v) const {
  return v->ref() == 1;
}


void Form::Delete(Proc* proc, Val* v) {
  FREE_COUNTED(proc, v);
}


// ----------------------------------------------------------------------------
// Implementation of BoolForm

BoolVal* BoolForm::NewVal(Proc* proc, bool val) {
  BoolVal* v = ALLOC_COUNTED(proc, BoolVal, sizeof(BoolVal));
  v->form_ = this;
  v->ref_ = 1;
  v->val_ = val;
  return v;
}


uint64 BoolForm::basic64(Val* v) const {
  return v->as_bool()->val() != 0;
}


Val* BoolForm::NewValBasic64(Proc* proc, Type* type, uint64 bits) {
  return NewVal(proc, bits != 0);
}


bool BoolForm::IsEqual(Val* v1, Val* v2) const {
  assert(v1->is_bool());
  return v2->is_bool() && v1->as_bool()->val() == v2->as_bool()->val();
}


Val* BoolForm::Cmp(Val* v1, Val* v2) const {
  assert(v1->is_bool());
  assert(v2->is_bool());
  return cmp(v1->as_bool()->val(), v2->as_bool()->val());
}


int BoolForm::Format(Proc* proc, Fmt::State* f, Val* v) const {
  return F.fmtprint(f, "%s", v->as_bool()->val() ? "true" : "false");
}


Val* BoolForm::Uniq(Proc* proc, Val* v) const {
  ShouldNotReachHere();
  return v;
}


uint32 BoolForm::Hash(Val* v) const {
  int64 b = v->as_bool()->val();
  return Hash32NumWithSeed(b, kHashSeed32);
}


szl_fingerprint BoolForm::Fingerprint(Proc* proc, Val* v) const {
  return ::Fingerprint(v->as_bool()->basic64());
}


// ----------------------------------------------------------------------------
// Implementation of IntForm

bool IntForm::IsEqual(Val* v1, Val* v2) const {
  assert(v1->is_int());
  return v2->is_int() && v1->as_int()->val() == v2->as_int()->val();
}


Val* IntForm::Cmp(Val* v1, Val* v2) const {
  assert(v1->is_int());
  assert(v2->is_int());
  return cmp(v1->as_int()->val(), v2->as_int()->val());
}


uint64 IntForm::basic64(Val* v) const {
  return v->as_int()->val();
}


Val* IntForm::NewValBasic64(Proc* proc, Type* type, uint64 bits) {
  return NewVal(proc, bits);
}


int IntForm::Format(Proc* proc, Fmt::State* f, Val* v) const {
  return F.fmtprint(f, "%lld", v->as_int()->val());
}


uint32 IntForm::Hash(Val* v) const {
  uint64 i = v->as_int()->val();
  return Hash32NumWithSeed(i, Hash32NumWithSeed((i>>32), kHashSeed32));
}


Val* IntForm::Uniq(Proc* proc, Val* v) const {
  ShouldNotReachHere();
  return v;
}


szl_fingerprint IntForm::Fingerprint(Proc* proc, Val* v) const {
  return ::Fingerprint(v->as_int()->basic64());
}


IntVal* IntForm::NewValInternal(Proc* proc, szl_int x) {
  assert(!TaggedInts::fits_smi(x));
  IntVal* v = ALLOC_COUNTED(proc, IntVal, sizeof(IntVal));
  v->form_ = this;
  v->ref_ = 1;
  v->val_ = x;
  return v;
}


// ----------------------------------------------------------------------------
// Implementation of UIntForm

bool UIntForm::IsEqual(Val* v1, Val* v2) const {
  assert(v1->is_uint());
  return v2->is_uint() && v1->as_uint()->val() == v2->as_uint()->val();
}


Val* UIntForm::Cmp(Val* v1, Val* v2) const {
  assert(v1->is_uint());
  assert(v2->is_uint());
  return cmp(v1->as_uint()->val(), v2->as_uint()->val());
}


uint64 UIntForm::basic64(Val* v) const {
  return v->as_uint()->val();
}


Val* UIntForm::NewValBasic64(Proc* proc, Type* type, uint64 bits) {
  return NewVal(proc, bits);
}


int UIntForm::Format(Proc* proc, Fmt::State* f, Val* v) const {
  return F.fmtprint(f, SZL_UINT_FMT, v->as_uint()->val());
}


uint32 UIntForm::Hash(Val* v) const {
  uint64 i = v->as_uint()->val();
  return Hash32NumWithSeed(i, Hash32NumWithSeed((i>>32), kHashSeed32));
}


Val* UIntForm::Uniq(Proc* proc, Val* v) const {
  ShouldNotReachHere();
  return v;
}


szl_fingerprint UIntForm::Fingerprint(Proc* proc, Val* v) const {
  return ::Fingerprint(v->as_uint()->basic64());
}


UIntVal* UIntForm::NewVal(Proc* proc, szl_uint val) {
  UIntVal* v = ALLOC_COUNTED(proc, UIntVal, sizeof(UIntVal));
  v->form_ = this;
  v->ref_ = 1;
  v->val_ = val;
  return v;
}


// ----------------------------------------------------------------------------
// Implementation of FingerprintForm

FingerprintVal* FingerprintForm::NewVal(Proc* proc, szl_fingerprint val) {
  FingerprintVal* v = ALLOC_COUNTED(proc, FingerprintVal,
                                    sizeof(FingerprintVal));
  v->form_ = this;
  v->ref_ = 1;
  v->val_ = val;
  return v;
}


uint64 FingerprintForm::basic64(Val* v) const {
  return v->as_fingerprint()->val();
}


Val* FingerprintForm::NewValBasic64(Proc* proc, Type* type, uint64 bits) {
  return NewVal(proc, bits);
}


bool FingerprintForm::IsEqual(Val* v1, Val* v2) const {
  assert(v1->is_fingerprint());
  return (v2->is_fingerprint() &&
          v1->as_fingerprint()->val() == v2->as_fingerprint()->val());
}


Val* FingerprintForm::Cmp(Val* v1, Val* v2) const {
  assert(v1->is_fingerprint());
  assert(v2->is_fingerprint());
  return cmp(v1->as_fingerprint()->val(), v2->as_fingerprint()->val());
}


int FingerprintForm::Format(Proc* proc, Fmt::State* f, Val* v) const {
  return F.fmtprint(f, SZL_FINGERPRINT_FMT, v->as_fingerprint()->val());
}


Val* FingerprintForm::Uniq(Proc* proc, Val* v) const {
  ShouldNotReachHere();
  return v;
}


uint32 FingerprintForm::Hash(Val* v) const {
  uint64 i = v->basic64();
  return Hash32NumWithSeed(i, Hash32NumWithSeed((i>>32), kHashSeed32));
}


szl_fingerprint FingerprintForm::Fingerprint(Proc* proc, Val* v) const {
  return ::Fingerprint(v->as_fingerprint()->basic64());
}


// ----------------------------------------------------------------------------
// Implementation of FloatForm

FloatVal* FloatForm::NewVal(Proc* proc, szl_float val) {
  FloatVal* v = ALLOC_COUNTED(proc, FloatVal, sizeof(FloatVal));
  v->form_ = this;
  v->ref_ = 1;
  v->val_ = val;
  return v;
}


uint64 FloatForm::basic64(Val* v) const {
  szl_float x = v->as_float()->val();
  return *pun_cast<const uint64*>(&x);
}


Val* FloatForm::NewValBasic64(Proc* proc, Type* type, uint64 bits) {
  szl_float x = *pun_cast<const szl_float*>(&bits);
  return NewVal(proc, x);
}


bool FloatForm::IsEqual(Val* v1, Val* v2) const {
  assert(v1->is_float());
  return v2->is_float() && v1->as_float()->val() == v2->as_float()->val();
}


Val* FloatForm::Cmp(Val* v1, Val* v2) const {
  assert(v1->is_float());
  assert(v2->is_float());
  return cmp(v1->as_float()->val(), v2->as_float()->val());
}


int FloatForm::Format(Proc* proc, Fmt::State* f, Val* v) const {
  char buf[64];
  FloatToAscii(buf, v->as_float()->val());
  return Fmt::fmtstrcpy(f, buf);
}


Val* FloatForm::Uniq(Proc* proc, Val* v) const {
  ShouldNotReachHere();
  return v;
}


uint32 FloatForm::Hash(Val* v) const {
  uint64 i = v->as_float()->basic64();
  return Hash32NumWithSeed(i, Hash32NumWithSeed((i>>32), kHashSeed32));
}


szl_fingerprint FloatForm::Fingerprint(Proc* proc, Val* v) const {
  return ::Fingerprint(v->as_float()->basic64());
}


// ----------------------------------------------------------------------------
// Implementation of TimeForm

TimeVal* TimeForm::NewVal(Proc* proc, szl_time val) {
  TimeVal* v = ALLOC_COUNTED(proc, TimeVal, sizeof(TimeVal));
  v->form_ = this;
  v->ref_ = 1;
  v->val_ = val;
  return v;
}


uint64 TimeForm::basic64(Val* v) const {
  return v->as_time()->val();
}


Val* TimeForm::NewValBasic64(Proc* proc, Type* type, uint64 bits) {
  return NewVal(proc, bits);
}


bool TimeForm::IsEqual(Val* v1, Val* v2) const {
  assert(v1->is_time());
  return v2->is_time() && v1->as_time()->val() == v2->as_time()->val();
}


Val* TimeForm::Cmp(Val* v1, Val* v2) const {
  assert(v1->is_time());
  assert(v2->is_time());
  return cmp(v1->as_time()->val(), v2->as_time()->val());
}


int TimeForm::Format(Proc* proc, Fmt::State* f, Val* v) const {
  char buf[kMaxTimeStringLen + 1];
  if (f->flags & Fmt::FmtSharp) {  // %#V means format is 1234T
    return F.fmtprint(f, "%lldT", v->as_time()->val());
  } else if (SzlTime2Str(v->as_time()->val(), "", &buf)) {
    // default format "Wed Dec 31 16:16:40 PST 1969"
    return F.fmtprint(f, "%q", buf);  // FIX THIS: should be "T%q"
  } else {
    return F.fmtprint(f, kStringForInvalidTime);
  }
}


Val* TimeForm::Uniq(Proc* proc, Val* v) const {
  ShouldNotReachHere();
  return v;
}


uint32 TimeForm::Hash(Val* v) const {
  uint64 i = v->basic64();
  return Hash32NumWithSeed(i, Hash32NumWithSeed((i>>32), kHashSeed32));
}


szl_fingerprint TimeForm::Fingerprint(Proc* proc, Val* v) const {
  return ::Fingerprint(v->as_time()->basic64());
}


// ----------------------------------------------------------------------------
// Implementation of BytesForm

bool BytesForm::IsEqual(Val* v1, Val* v2) const {
  assert(v1->is_bytes());
  if (!v2->is_bytes())
    return false;
  BytesVal* b1 = v1->as_bytes();
  BytesVal* b2 = v2->as_bytes();
  return b1->length() == b2->length() &&
         memcmp(b1->base(), b2->base(), b1->length()) == 0;
}


Val* BytesForm::Cmp(Val* v1, Val* v2) const {
  assert(v1->is_bytes());
  assert(v2->is_bytes());
  BytesVal* b1 = v1->as_bytes();
  BytesVal* b2 = v2->as_bytes();
  int d = memcmp(b1->base(), b2->base(), std::min(b1->length(), b2->length()));
  if (d != 0)
    return TaggedInts::MakeVal(d);
  return cmp(b1->length(), b2->length());
}


BytesVal* BytesForm::NewVal(Proc* proc, int length) {
  BytesVal* v = ALLOC_COUNTED(proc, BytesVal, sizeof(BytesVal) + length);
  v->form_ = this;
  v->ref_ = 1;
  v->SetRange(0, length);
  v->array_ = v;
  return v;
}


BytesVal* BytesForm::NewValInit(Proc* proc, int length, const char* x) {
  BytesVal* v = NewVal(proc, length);
  memmove(v->base(), x, length);
  return v;
}


// TODO: this is almost identical to the other NewSlices; fold them together
BytesVal* BytesForm::NewSlice(Proc* proc, BytesVal* v, int origin, int length) {
  assert(v->ref() > 0);
  // If the ref count is one we can just overwrite this BytesVal
  // (Note that for slices the ref count of the array is not relevant.)
  if (v->ref() == 1) {
    v->SetSubrange(origin, length);
    return v;
  }
  BytesVal* n = ALLOC_COUNTED(proc, BytesVal, sizeof(BytesVal));
  n->form_ = this;
  n->ref_ = 1;
  n->SetRange(v->origin() + origin, length);
  n->array_ = v->array_;
  v->array_->inc_ref();
  v->dec_ref();
  return n;
}


void BytesForm::Delete(Proc* proc, Val* v) {
  BytesVal* b = v->as_bytes();
  if (b->array_ != v)
    b->array_->dec_ref_and_check(proc);
  FREE_COUNTED(proc, b);
}


void BytesForm::AdjustHeapPtrs(Proc* proc, Val* v) {
  assert(v->ref() > 0 && !v->is_readonly());
  BytesVal* b = v->as_bytes();
  b->array_ = proc->heap()->AdjustPtr(b->array_);
}


void BytesForm::CheckHeapPtrs(Proc* proc, Val* v) {
  CHECK_GT(v->ref(), 0);
  BytesVal* b = v->as_bytes();
  if (!v->is_readonly())
    proc->heap()->CheckPtr(b->array_);
}


int BytesForm::Format(Proc* proc, Fmt::State* f, Val* v) const {
  BytesVal* b = v->as_bytes();
  bool is_ascii = true;
  for (int i = 0; i < b->length(); i++) {
    int c = b->at(i);
    if (c < ' ' || '~' < c) {
      is_ascii = false;
      break;
    }
  }
  if (is_ascii) {
    F.fmtprint(f, "B%.*q", b->length(), b->base());
  } else {
    F.fmtprint(f, "X\"");
    for (int i = 0; i < b->length(); i++) {
      static char kHex[] = "0123456789ABCDEF";
      F.fmtprint(f, "%c%c", kHex[(b->at(i) >> 4) & 0xF], kHex[b->at(i) & 0xF]);
    }
    F.fmtprint(f, "\"");
  }
  return 0;
}


bool BytesForm::IsUnique(const Val* v) const {
  return v->as_bytes()->is_unique();
}


Val* BytesForm::Uniq(Proc* proc, Val* v) const {
  BytesVal* b = v->as_bytes();
  // take care to check the ref count of the original too if this is a slice
  if (!b->is_unique()) {
    // make a copy
    TRACE_REF("uniquing bytes", b);
    BytesVal* newb = Factory::NewBytes(proc, b->length());
    memmove(newb->base(), b->base(), b->length());
    b->dec_ref();
    b = newb;
  }
  CHECK(b->is_unique());
  return b;
}


uint32 BytesForm::Hash(Val* v) const {
  BytesVal* b = v->as_bytes();
  // It's an array of scalars.  They will be densely packed, so
  // it's safe to fingerprint the contents of the array directly.
  return Hash32StringWithSeed(b->base(), b->length(), kHashSeed32);
}


szl_fingerprint BytesForm::Fingerprint(Proc* proc, Val* v) const {
  BytesVal *b = v->as_bytes();
  // Elements are scalar
  return ::FingerprintString(b->base(), b->length());
}


// ----------------------------------------------------------------------------
// Implementation of StringForm

bool StringForm::IsEqual(Val* v1, Val* v2) const {
  assert(v1->is_string());
  if (!v2->is_string())
    return false;
  StringVal* s1 = v1->as_string();
  StringVal* s2 = v2->as_string();
  return s1->length() == s2->length() &&
         memcmp(s1->base(), s2->base(), s1->length()) == 0;
}


// like strncmp, but unsigned and without check for 0 termination.
static int mystrcmp(const unsigned char* s1, const unsigned char* s2, int n) {
  int i = 0;
  while (i < n && s1[i] == s2[i])
    i++;
  return i < n ? (s1[i] - s2[i]) : 0;
}


Val* StringForm::Cmp(Val* v1, Val* v2) const {
  assert(v1->is_string());
  assert(v2->is_string());
  StringVal* s1 = v1->as_string();
  StringVal* s2 = v2->as_string();
  const int l = std::min(s1->length(), s2->length());
  const int d = mystrcmp(s1->u_base(), s2->u_base(), l);
  if (d != 0)
    return TaggedInts::MakeVal(d);
  return cmp(s1->length(), s2->length());
}


// Compute the size required to store a string. The size
// a) must be >= sizeof(SliceInfo)
// b) should include space wasted by alignment so we can grow in place
// c) should include extra slop, even if alignment is perfect
// TODO: Should we add even more slop if string is large?
static int AmountToAllocate(int length) {
  // The alignment is the same as used in memory.cc, so we can
  // always use the alignment space for free. Add one byte to
  // guarantee that we always have some slop.
  length = Align(length + 1 + sizeof(int64), sizeof(int64));
  if (length < sizeof(SliceInfo))
    length = sizeof(SliceInfo);
  return length;
}


StringVal* StringForm::NewVal(Proc* proc, int length, int num_runes) {
  assert(num_runes >= 0);
  int size = AmountToAllocate(length);
  StringVal* v = ALLOC_COUNTED(proc, StringVal,
                               sizeof(StringVal) - sizeof(SliceInfo) + size);
  v->form_ = this;
  v->ref_ = 1;
  v->map_ = NULL;
  v->size_ = size;
  v->SetRange(proc, 0, length, num_runes);
  // StringVals that are allocated as part of static initialization or
  // in 'persistent' memory must have a map
  if (!proc->is_initialized() || (proc->mode() & Proc::kPersistent) != 0)
    v->AllocateOffsetMap(proc);
  return v;
}


StringVal* StringForm::NewValInitCStr(Proc* proc, szl_string str) {
  bool input_is_valid;
  int num_runes;
  int nbytes = CStrValidUTF8Len(str, &input_is_valid, &num_runes);
  StringVal* v = NewVal(proc, nbytes, num_runes);
  if (input_is_valid)
    memmove(v->base(), str, nbytes);
  else
    CStr2ValidUTF8(v->base(), str);
  return v;
}


StringVal* StringForm::NewValInit(Proc* proc, int length, const char* str) {
  bool input_is_valid;
  int num_runes;
  int nbytes = StrValidUTF8Len(str, length, &input_is_valid, &num_runes);
  StringVal* v = NewVal(proc, nbytes, num_runes);
  if (input_is_valid)
    memmove(v->base(), str, nbytes);
  else
    Str2ValidUTF8(v->base(), str, length);
  return v;
}


StringVal* StringForm::NewSlice(Proc* proc, StringVal* v, int origin,
                                int length, int num_runes) {
  assert(num_runes >= 0);
  assert(v->ref() > 0);
  // If already a slice and the ref count is one we can just overwrite this
  // StringVal.
  // (Note that for slices the ref count of the array is not relevant.)
  if (v->is_slice() && v->ref() == 1) {
    v->SetSubrange(proc, origin, length, num_runes);
    return v;
  }
  StringVal* n = ALLOC_COUNTED(proc, StringVal, sizeof(StringVal));
  n->form_ = this;
  n->ref_ = 1;
  n->map_ = NULL;
  n->size_ = -1;  // we have a slice
  n->SetRange(proc, v->origin() + origin, length, num_runes);
  n->slice_.array = v->array();
  // StringVals that are allocated as part of static initialization or
  // in 'persistent' memory must have a map
  if (!proc->is_initialized() || (proc->mode() & Proc::kPersistent) != 0)
    n->AllocateOffsetMap(proc);
  v->array()->inc_ref();
  v->dec_ref();
  return n;
}


void StringForm::Delete(Proc* proc, Val* v) {
  assert(!v->is_readonly());
  StringVal* s = v->as_string();
  if (s->array() != v)
    s->array()->dec_ref_and_check(proc);
  if (s->map_ != NULL && s->map_ != &StringVal::ASCIIMap)
    FREE(proc, s->map_);
  FREE_COUNTED(proc, s);
}


void StringForm::AdjustHeapPtrs(Proc* proc, Val* v) {
  assert(v->ref() > 0  || v->is_readonly());  // allow readonly string objects
  StringVal* s = v->as_string();
  if (s->is_slice()) {
    assert(!v->is_readonly());
    s->slice_.array = proc->heap()->AdjustPtr(s->slice_.array);
  }
  if (s->map_ != NULL && s->map_ != &StringVal::ASCIIMap) {
    assert(!v->is_readonly());
    s->map_ = proc->heap()->AdjustPtr(s->map_);
  }
}


void StringForm::CheckHeapPtrs(Proc* proc, Val* v) {
  CHECK_GT(v->ref(), 0);
  StringVal* s = v->as_string();
  if (s->is_slice()) {
    CHECK(!v->is_readonly());
    proc->heap()->CheckPtr(s->slice_.array);
  }
  if (s->map_ != NULL && s->map_ != &StringVal::ASCIIMap) {
    CHECK(!v->is_readonly());
    proc->heap()->CheckPtr(s->map_);
  }
}


int StringForm::Format(Proc* proc, Fmt::State* f, Val* v) const {
  StringVal* s = v->as_string();
  return F.fmtprint(f, "%.*q", s->length(), s->base());
  return 0;
}


bool StringForm::IsUnique(const Val* v) const {
  return v->as_string()->is_unique();
}


Val* StringForm::Uniq(Proc* proc, Val* v) const {
  StringVal* s = v->as_string();
  // take care to check the ref count of the original too if this is a slice
  if (!s->is_unique()) {
    // make a copy
    TRACE_REF("uniquing string", s);
    StringVal* news = Factory::NewStringBytes(proc, s->length(), s->base());
    s->dec_ref();
    s = news;
  }
  CHECK(s->is_unique());
  return s;
}


uint32 StringForm::Hash(Val* v) const {
  StringVal* s = v->as_string();
  // It's an array of scalars.  They will be densely packed, so
  // it's safe to fingerprint the contents of the array directly.
  return Hash32StringWithSeed(s->base(), s->length(), kHashSeed32);
}


szl_fingerprint StringForm::Fingerprint(Proc* proc, Val* v) const {
  StringVal* s = v->as_string();
  // Elements are scalar
  return ::FingerprintString(s->base(), s->length());
}


// ----------------------------------------------------------------------------
// Implementation of ArrayForm

bool ArrayForm::IsEqual(Val* v1, Val* v2) const {
  assert(v1->is_array());
  if (!v2->is_array())
    return false;
  ArrayVal* a1 = v1->as_array();
  ArrayVal* a2 = v2->as_array();
  assert(a1->type()->IsEqual(a2->type(), false));  // ignore proto info
  if (a1->length() != a2->length())
    return false;
  for (int i = a1->length(); i-- > 0; )
    if (!a1->at(i)->IsEqual(a2->at(i)))
      return false;
  return true;
}

Val* ArrayForm::Cmp(Val* v1, Val* v2) const {
  assert(v1->is_array());
  if (!v2->is_array())
    return false;
  ArrayVal* a1 = v1->as_array();
  ArrayVal* a2 = v2->as_array();
  assert(a1->type()->IsEqual(a2->type(), false));  // ignore proto info

  int l1 = a1->length();
  int l2 = a2->length();
  for (int i = 0; i < l1 && i < l2; ++i) {
    Val* d = a1->at(i)->Cmp(a2->at(i));
    if (!TaggedInts::is_zero(d))
      return d;
  }
  return cmp(l1, l2);
}

ArrayVal* ArrayForm::NewVal(Proc* proc, int length) {
  ArrayVal* v = ALLOC_COUNTED(proc, ArrayVal,
                              sizeof(ArrayVal) + length * sizeof(Val*));
  v->form_ = this;
  v->ref_ = 1;
  v->SetRange(0, length);
  v->array_ = v;
  return v;
}


ArrayVal* ArrayForm::NewValInit(Proc* proc, int length, Val* init_val) {
  ArrayVal* v = NewVal(proc, length);
  for (int i = length; i-- > 0; )
    v->at(i) = init_val;
  return v;
}


ArrayVal* ArrayForm::NewSlice(Proc* proc, ArrayVal* v, int origin, int length) {
  assert(v->ref() > 0);
  // If the ref count is one we can just overwrite this ArrayVal
  // (Note that for slices the ref count of the array is not relevant.)
  if (v->ref() == 1) {
    v->SetSubrange(origin, length);
    return v;
  }
  ArrayVal* n = ALLOC_COUNTED(proc, ArrayVal, sizeof(ArrayVal));
  n->form_ = this;
  n->ref_ = 1;
  n->SetRange(v->origin() + origin, length);
  n->array_ = v->array_;
  v->array_->inc_ref();
  v->dec_ref();
  return n;
}


void ArrayForm::Delete(Proc* proc, Val* v) {
  ArrayVal* av = v->as_array();
  if (av->array_ == v) {
    for (int i = av->length(); i-- > 0; )
      av->at(i)->dec_ref_and_check(proc);
  } else {
    av->array_->dec_ref_and_check(proc);
  }
  FREE_COUNTED(proc, av);
}


void ArrayForm::AdjustHeapPtrs(Proc* proc, Val* v) {
  assert(v->ref() > 0 && !v->is_readonly());
  ArrayVal* av = v->as_array();
  if (av->array_ == av) {
    Memory* heap = proc->heap();
    for (int i = av->length(); i-- > 0; ) {
      Val*& v = av->at(i);
      v = heap->AdjustVal(v);
    }
  }
  av->array_ = proc->heap()->AdjustPtr(av->array_);
}


void ArrayForm::CheckHeapPtrs(Proc* proc, Val* v) {
  CHECK_GT(v->ref(), 0);
  ArrayVal* av = v->as_array();
  if (!v->is_readonly())
    proc->heap()->CheckPtr(av->array_);
  if (av->array_ == av) {
    Memory* heap = proc->heap();
    for (int i = av->length(); i-- > 0; ) {
      Val* v = av->at(i);
      heap->CheckVal(v);
    }
  }
}


int ArrayForm::Format(Proc* proc, Fmt::State* f, Val* v) const {
  ArrayVal* a = v->as_array();
  const int n = a->length();
  int e = Fmt::fmtstrcpy(f, "{ ");
  for (int i = 0; i < n; i++) {
    if (i > 0)
      e += Fmt::fmtstrcpy(f, ", ");
    e += a->at(i)->Format(proc, f);
  }
  e += Fmt::fmtstrcpy(f, " }");
  return e;
}


bool ArrayForm::IsUnique(const Val* v) const {
  return v->as_array()->is_unique();
}


Val* ArrayForm::Uniq(Proc* proc, Val* v) const {
  ArrayVal* a = v->as_array();
  // take care to check the ref count of the original too if this is a slice
  if (!a->is_unique()) {
    // make a copy
    TRACE_REF("uniquing array", a);
    ArrayVal* newa = a->type()->as_array()->form()->NewVal(proc, a->length());
    for (int i = 0; i < a->length(); i++) {
      Val* e = a->at(i);
      newa->at(i) = e;
      e->inc_ref();
    }
    a->dec_ref();
    a = newa;
  }
  CHECK(a->is_unique());
  return a;
}


uint32 ArrayForm::Hash(Val* v) const {
  ArrayVal* a = v->as_array();
  // Note that all zero-length arrays will have same
  // fingerprint, though, regardless of type.  This is not an
  // issue since a map has a fixed type.
  uint32 hash = kHashSeed32;

  if (a->length() == 0)
    return hash;
  // The elements must be pointer types; bytes and string are
  // handled separately.
  for (int i = 0; i < a->length(); i++) {
    Val* elem = a->at(i);
    hash = MapHashCat(hash, elem->form()->Hash(elem));
  }
  return hash;
}


szl_fingerprint ArrayForm::Fingerprint(Proc* proc, Val* v) const {
  // Prime the pump so an empty array has a fingerprint != 0.
  // Note that all zero-length arrays will have same
  // fingerprint, though, regardless of type.  This is not an
  // issue since an array has a fixed type.
  szl_fingerprint print = kFingerSeed();
  ArrayVal* a = v->as_array();
  for (int i = 0; i < a->length(); i++)
    print = FingerprintCat(print, a->at(i)->Fingerprint(proc));
  return print;
}


// ----------------------------------------------------------------------------
// Implementation of MapForm

bool MapForm::IsEqual(Val* v1, Val* v2) const {
  assert(v1->is_map());
  return v2->is_map() && v1->as_map()->map()->EqualMap(v2->as_map()->map());
}


Val* MapForm::Cmp(Val* v1, Val* v2) const {
  return NULL;
}


MapVal* MapForm::NewVal(Proc* proc) {
  MapVal* v = ALLOC_COUNTED(proc, MapVal, sizeof(MapVal));
  v->form_ = this;
  v->ref_ = 1;
  return v;
}


MapVal* MapForm::NewValInit(Proc* proc, int occupancy, bool exact) {
  MapVal* v = NewVal(proc);
  v->InitMap(proc, occupancy, exact);
  return v;
}


void MapForm::Delete(Proc* proc, Val* v) {
  MapVal* mv = v->as_map();
  mv->map_->Delete();
  FREE_COUNTED(proc, mv);
}


void MapForm::AdjustHeapPtrs(Proc* proc, Val* v) {
  assert(v->ref() > 0 && !v->is_readonly());
  MapVal* mv = v->as_map();
  // Since the Map object is not a Val its internal pointers will not be
  // adjusted during compaction, so we have to do it now.
  // Note that this means that each Map must be referenced by only one MapVal.
  // TODO: consider merging the contents of Map into MapVal.
  mv->map_->AdjustHeapPtrs();
  mv->map_ = proc->heap()->AdjustPtr(mv->map_);
}


void MapForm::CheckHeapPtrs(Proc* proc, Val* v) {
  CHECK_GT(v->ref(), 0);
  MapVal* mv = v->as_map();
  if (!v->is_readonly())
    proc->heap()->CheckPtr(mv->map_);
  mv->map_->CheckHeapPtrs();
}


int MapForm::Format(Proc* proc, Fmt::State* f, Val* v) const {
  return v->as_map()->map()->FmtMap(f);
}


Val* MapForm::Uniq(Proc* proc, Val* v) const {
  MapVal* m = v->as_map();
  if (!m->is_unique()) {
    // make a copy
    TRACE_REF("uniquing map", m);
    MapVal* newval = m->type()->as_map()->form()->NewVal(proc);
    newval->set_map(m->map()->Clone());  // the implementation is in map.cc
    m->dec_ref();
    m = newval;
  }
  CHECK(m->is_unique());
  return m;
}


uint32 MapForm::Hash(Val* v) const {
  uint64 i = v->as_map()->map()->Fingerprint();
  return Hash32NumWithSeed(i, Hash32NumWithSeed((i>>32), kHashSeed32));
}


szl_fingerprint MapForm::Fingerprint(Proc* proc, Val* v) const {
  return v->as_map()->map()->Fingerprint();
}


// ----------------------------------------------------------------------------
// Implementation of TupleForm

bool TupleForm::IsEqual(Val* v1, Val* v2) const {
  assert(v1->is_tuple());
  if (!v2->is_tuple())
    return false;
  TupleVal* t1 = v1->as_tuple();
  TupleVal* t2 = v2->as_tuple();
  assert(t1->type()->IsEqual(t2->type(), false));  // ignore proto info
  // TODO: consider using assert instead of CHECK for the AllFieldsRead() checks
  CHECK(t1->type()->as_tuple()->AllFieldsRead());
  for (int i = t1->type()->as_tuple()->nslots(); i-- > 0; )
    if (!t1->slot_at(i)->IsEqual(t2->slot_at(i)))
      return false;
  return true;
}

Val* TupleForm::Cmp(Val* v1, Val* v2) const {
  assert(v1->is_tuple());
  if (!v2->is_tuple())
    return false;
  TupleVal* t1 = v1->as_tuple();
  TupleVal* t2 = v2->as_tuple();
  assert(t1->type()->IsEqual(t2->type(), false));  // ignore proto info

  CHECK(t1->type()->as_tuple()->AllFieldsRead());
  CHECK(t2->type()->as_tuple()->AllFieldsRead());
  int l1 = t1->type()->as_tuple()->nslots();
  int l2 = t2->type()->as_tuple()->nslots();
  for (int i = 0; i < l1 && i < l2; ++i) {
    Val* d = t1->slot_at(i)->Cmp(t2->slot_at(i));
    if (!TaggedInts::is_zero(d))
      return d;
  }
  return cmp(l1, l2);
}


TupleVal* TupleForm::NewVal(Proc* proc, InitMode mode) {
  TupleType* tt = type()->as_tuple();
  const int n = tt->nslots();
  const int t = tt->ntotal();
  TupleVal* v = ALLOC_COUNTED(proc, TupleVal,
                              sizeof(TupleVal) + t * sizeof(Val*));
  switch (mode) {
    case ignore_inproto:
      // nothing to do
      break;
    case clear_inproto:
      memset(v->base() + n, 0, (t - n) * sizeof(Val*));
      break;
    case set_inproto:
      memset(v->base() + n, -1, (t - n) * sizeof(Val*));
      break;
  }
  v->form_ = this;
  v->ref_ = 1;
  return v;
}


void TupleForm::Delete(Proc* proc, Val* v) {
  TupleVal* t = v->as_tuple();
  Val** slots = t->base();
  int nslots = t->type()->as_tuple()->nslots();
  for (int i = 0; i < nslots; i++)
    slots[i]->dec_ref_and_check(proc);
  FREE_COUNTED(proc, t);
}


void TupleForm::AdjustHeapPtrs(Proc* proc, Val* v) {
  assert(v->ref() > 0 || v->is_readonly());  // allow readonly tuple objects
  TupleVal* t = v->as_tuple();
  Memory* heap = proc->heap();
  Val** slots = t->base();
  int nslots = t->type()->as_tuple()->nslots();
  for (int i = 0; i < nslots; i++) {
    Val*& v = slots[i];
    v = heap->AdjustVal(v);
  }
}


void TupleForm::CheckHeapPtrs(Proc* proc, Val* v) {
  CHECK_GT(v->ref(), 0);
  TupleVal* t = v->as_tuple();
  Memory* heap = proc->heap();
  Val** slots = t->base();
  int nslots = t->type()->as_tuple()->nslots();
  for (int i = 0; i < nslots; i++) {
    Val*& v = slots[i];
    heap->CheckVal(v);
  }
}


int TupleForm::Format(Proc* proc, Fmt::State* f, Val* v) const {
  TupleVal* t = v->as_tuple();
  // Emit all fields, even if unreferenced.
  // If we are doing a conversion, all fields should be marked referenced.
  // Otherwise we are generating debug output (e.g. stack trace) and omitting
  // an unreferenced field would be misleading.
  List<Field*>* fields = t->type()->as_tuple()->fields();
  const int n = fields->length();
  int e = Fmt::fmtstrcpy(f, "{ ");
  for (int i = 0; i < n; i++) {
    if (i > 0)
      e += Fmt::fmtstrcpy(f, ", ");
    Field* field = fields->at(i);
    if (field->read())
      e += t->field_at(field)->Format(proc, f);
    else
      e += Fmt::fmtstrcpy(f, "<unused>");
  }
  e += Fmt::fmtstrcpy(f, " }");
  return e;
}


Val* TupleForm::Uniq(Proc* proc, Val* v) const {
  TupleVal* t = v->as_tuple();
  if (!t->is_unique()) {
    // make a copy
    TRACE_REF("uniquing tuple", t);
    TupleVal* newt = t->type()->as_tuple()->form()->NewVal(proc,
                                                           ignore_inproto);
    const int n = t->type()->as_tuple()->nslots();
    const int m = t->type()->as_tuple()->ntotal();
    // copy slots
    int i = 0;
    while (i < n) {
      Val* e = t->slot_at(i);
      newt->slot_at(i) = e;
      e->inc_ref();
      i++;
    }
    // copy inproto bits
    while (i < m) {
      newt->base()[i] = t->base()[i];  // use base() to avoid index range check
      i++;
    }
    // done
    t->dec_ref();
    t = newt;
  }
  CHECK(t->is_unique());
  return t;
}


uint32 TupleForm::Hash(Val* v) const {
  TupleVal* t = v->as_tuple();
  // Note that all zero-length tuples will have same
  // fingerprint, though, regardless of type.  This is not an
  // issue since a map has a fixed type.
  uint32 hash = kHashSeed32;

  TupleType* ttype = t->type()->as_tuple();
  CHECK(ttype->AllFieldsRead());
  for (int i = 0; i < ttype->nslots(); i++) {
    Val* elem = t->slot_at(i);
    hash = MapHashCat(hash, elem->form()->Hash(elem));
  }
  return hash;
}


szl_fingerprint TupleForm::Fingerprint(Proc* proc, Val* v) const {
  // Prime the pump so an empty tuple has a fingerprint != 0.
  szl_fingerprint print = kFingerSeed();

  TupleVal* t = v->as_tuple();
  for (int i = 0; i < t->type()->as_tuple()->nslots(); i++)
    print = FingerprintCat(print, t->slot_at(i)->Fingerprint(proc));
  return print;
}


// ----------------------------------------------------------------------------
// Implementation of ClosureForm

ClosureVal* ClosureForm::NewVal(Proc* proc, Instr* entry, Frame* context) {
  ClosureVal* c = ALLOC_COUNTED(proc, ClosureVal, sizeof(ClosureVal));
  c->form_ = this;
  c->ref_ = 1;
  c->entry_ =  entry;
  c->context_ = context;
  return c;
}


bool ClosureForm::IsEqual(Val* v1, Val* v2) const {
  assert(v1->is_closure());
  if (!v2->is_closure())
    return false;
  ClosureVal* cv1 = v1->as_closure();
  ClosureVal* cv2 = v2->as_closure();
  return cv1->entry() == cv2->entry() && cv1->context() == cv2->context();
  return false;
}


Val* ClosureForm::Cmp(Val* v1, Val* v2) const {
  assert(v1->is_closure());
  assert(v2->is_closure());
  ClosureVal* cv1 = v1->as_closure();
  ClosureVal* cv2 = v2->as_closure();
  if (cv1->entry() != cv2->entry())
    return cmp(cv1->entry(), cv2->entry());
  else
    return cmp(cv1->context(), cv2->context());
}


int ClosureForm::Format(Proc* proc, Fmt::State* f, Val* v) const {
  ClosureVal* c = v->as_closure();
  if (proc != NULL) {
    Code* code = proc->code();
    Instr* pc = c->entry();
    Function* fun = code->FunctionForInstr(pc);
    assert(fun != NULL);
    if (fun->name() != NULL)
      return F.fmtprint(f, "%s", fun->name());
    else
      return F.fmtprint(f, "%N", fun);
  } else {
    // When printing using %V the caller must be careful to pass a non-NULL
    // value for proc if the value could be a ClosureVal.  Since most of the
    // xxxForm::Format() methods ignore proc, many uses of %V supply NULL for
    // proc and Val::ValFmt() passes it along without checking.  But if the
    // value is a ClosureVal, a non-null proc is required when we get here.
    ShouldNotReachHere();
    return 0;
  }
}


Val* ClosureForm::Uniq(Proc* proc, Val* v) const {
  ShouldNotReachHere();
  return NULL;
}


uint32 ClosureForm::Hash(Val* v) const {
  // For the fingerprint we use the code offset and the dynamic level
  // because they are the same in different shards and even across different
  // runs.  But for the hash we only need a consistent value in this execution,
  // so we can use the actual code and context pointers.
  ClosureVal* cv = v->as_closure();
  uint32 fct_hash = Hash32PointerWithSeed(cv->entry(), kHashSeed32);
  uint32 context_hash = Hash32PointerWithSeed(cv->context(), kHashSeed32);
  return MapHashCat(fct_hash, context_hash);
}


szl_fingerprint ClosureForm::Fingerprint(Proc* proc, Val* v) const {
  // Use a combination of the code index and the dynamic level.
  // This should be sufficient to be unique within this program yet give
  // identical results across multiple shards.
  // Note that the fingerprint is only useful as long as the closure is valid,
  // since exiting the closure's context and entering it again on another
  // call chain with the same dynamic level will give the same fingerprint.
  ClosureVal* cv = v->as_closure();
  Code* code = proc->code();
  Instr* pc = cv->entry();
  // TODO: this does a linear search over the code segments.  We could fix
  // this by storing a pointer to the Function right before the code for that
  // function.
  int index = code->DescForInstr(pc)->index();
  return FingerprintCat(::Fingerprint(index), cv->dynamic_level(proc));
}


}  // namespace sawzall
