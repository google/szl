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

#include <assert.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "utilities/strutils.h"

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
#include "engine/proc.h"
#include "engine/factory.h"
#include "engine/frame.h"


namespace sawzall {

// ----------------------------------------------------------------------------
// Implementation of Val

int Val::Format(Proc* proc, Fmt::State* f) {
  return form()->Format(proc, f, this);
}


uint64 Val::basic64() {
  return form()->basic64(this);
}


szl_fingerprint Val::Fingerprint(Proc* proc) {
  return form()->Fingerprint(proc, this);
}


int Val::ValFmt(Fmt::State* f) {
  Proc* proc = FMT_ARG(f, Proc*);
  Val* val = FMT_ARG(f, Val*);
  if (val == NULL)
    return Fmt::fmtstrcpy(f, "<undefined>");
  return val->Format(proc, f);
}


Val* Val::Uniq(Proc* proc) {
  return form()->Uniq(proc, this);
}


bool Val::IsEqual(Val* val) {
  return form()->IsEqual(this, val);
}


Val* Val::Cmp(Val* val) {
  return form()->Cmp(this, val);
}


//----------------------------------------------------------------------------
// Implementation of IndexableVal

void IndexableVal::intersect_slice(szl_int* beg, szl_int* end, szl_int length) {
  assert(ref_ >= 0);
  szl_int b = *beg;
  szl_int e = *end;
  szl_int n = length;
  if (b < 0)
    b = 0;
  if (e > n)
    e = n;
  // 0 <= b, e <= n
  if (b > e) {
    b = 0;
    e = 0;
  }
  // 0 <= b <= e <= n
  *beg = b;
  *end = e;
}


//----------------------------------------------------------------------------
// Implementation of OffsetMap


// Factory for OffsetMap.
OffsetMap* OffsetMap::New(Proc* proc) {
  OffsetMap* map = ALLOC(proc, OffsetMap, sizeof(OffsetMap));
  map->Reset();
  return map;
}


// Return the position advanced by delta characters.
// Assumes that it is possible to move delta characters and that
// the UTF-8 encoding is correct.
static char* forward_utf8(char* p, int delta) {
  // Move forwards delta characters.  Invariant: p points to
  // the first byte of the character to skip.  We step
  // across it.  If the next byte is
  //   10XXXXXX
  // it is the second byte of a multibyte sequence) and we should
  // continue skipping. If it's anything else, it's the first byte
  // of a character and we stop.
  while (delta-- > 0) {
    do
      p++;
    while ((*p & 0xC0) == 0x80);  // Continue if it's an continuation byte
  }
  return p;
}


// Return the position retarded by delta characters.
// Assumes that it is possible to move delta characters and that
// the UTF-8 encoding is correct.
static char* backward_utf8(char* p, int delta) {
  // Move backwards delta characters.  Invariant: p points to
  // the byte immediately after the character to back up.  We back
  // it up. The byte it reaches is either (in binary)
  //   0XXXXXXX  (ASCII)
  // or
  //   10XXXXXX  (second or subsequent byte of multibyte sequence)
  // We therefore loop until we hit the first byte of the character, which
  // will be either
  //   0XXXXXXX  (ASCII)
  // or
  //   11XXXXXX  (first byte of multibyte sequence).  The easiest way
  // to write the loop is to run backwards while the byte is
  //   10XXXXXX
  while (delta-- > 0) {
    do
      p--;
    while ((*p & 0xC0) == 0x80);
  }
  return p;
}


// Convert a rune index into the corresponding byte offset.
// No rune index boundary checking is done, instead the string
// is assumed to extend in both directions ad infinitum, with
// the missing characters assumed to be ASCII (1 byte long).
szl_int OffsetMap::byte_offset(StringVal* val, szl_int rune_index) {
  if (rune_index <= 0)  // assume "infinite" string
    return rune_index;

  int n = val->num_runes();  // n <= length()
  if (rune_index >= n)  // assume "infinite" string
    return val->length() + (rune_index - n);

  // 0 < rune_index < n
  char* p = val->base();
  // Find the nearest point from which to begin scanning.
  // d1: distance from beginning to rune_index (>0)
  // d2: distance from index to rune_index (can be <0)
  // d3: distance from rune_index to end (>0)
  int d2 = rune_index - index_;
  if (d2 < 0) {
    int d1 = rune_index;
    if (d1 < -d2)
      p = forward_utf8(p, d1);
    else
      p = backward_utf8(p + offset_, -d2);
  } else {
    int d3 = n - rune_index;
    if (d2 < d3)
      p = forward_utf8(p + offset_, d2);
    else
      p = backward_utf8(p + val->length(), d3);
  }
  index_ = rune_index;
  offset_ = p - val->base();
  return offset_;
}


//----------------------------------------------------------------------------
// Implementation of ArrayVal

void ArrayVal::PutSlice(Proc* proc, int beg, int end, ArrayVal* x) {
  assert(ref_ >= 0);
  assert(x->ref_ >= 0);
  assert(is_unique());  // must have been uniq'ed
  assert(beg == 0 || legal_index(beg - 1));
  assert(end == 0 || legal_index(end - 1));
  Val** dst = base() + beg;
  int dst_length = end - beg;
  Val** src = x->base();
  int src_length = x->length();

  // increase the ref count on the copied elements
  for (int i = 0; i < src_length; i++)
    x->at(i)->inc_ref();
  // decrease the ref count on the overwritten elements
  for (int i = beg; i < end; i++)
    at(i)->dec_ref();

  if (dst_length == src_length) {
    // replacing a slice with an array or slice of the same length
    // => simply replace the old one with the new one; no need to call SetRange
    if (dst_length == 1) {
      *dst = *src;
      return;
    }
    // otherwise just copy the values (below)
  } else {
    // since we have Uniq'd, if we have a slice we can use the entire original
    int old_length = length();
    int avail_length = array_->length() - origin();
    int new_length = old_length + src_length - dst_length;
    if (new_length <= avail_length) {
      // fits, just reposition the suffix before copying the assigned slice
      if (end != old_length)
        memmove(dst + src_length, dst + dst_length,
                (old_length - end) * sizeof(Val*));
      // zero out vacated slots (GC must not treat the contents as values)
      if (new_length < old_length)
        memset(base() + new_length, 0, (old_length - new_length) * sizeof(Val*));
      SetRange(origin(), new_length);
    } else {
      // does not fit; if it is a slice we might still have enough space
      // if we relocate the origin to zero; otherwise allocate a new value
      Val** dst_old_base = base();
      ArrayVal* old_array = array_;
      if (array_ == this || new_length > array_->length()) {
        // allocate a new array value and copy the prefix and suffix
        // make "this" a slice that refers to the new value
        // form_, ref_, size_ remain unchanged
        array_ = static_cast<ArrayForm*>(form())->NewVal(proc, new_length);
      }
      Val** dst_new_base = array_->base();
      if (beg != 0)
        memmove(dst_new_base, dst_old_base, beg * sizeof(Val*));
      dst = dst_new_base + beg;
      if (end != old_length)
        memmove(dst + src_length, dst_old_base + end,
                (old_length - end) * sizeof(Val*));
      // zero out vacated slots (GC must not treat the contents as values)
      if (array_ == old_array) {
        // reused the existing array; check for vacated slots at the end
        if (new_length < old_length)
          memset(base() + new_length, 0,
                 (old_length - new_length) * sizeof(Val*));
      } else {
        // copied to a new array; all slots in the old array were vacated
        memset(dst_old_base, 0, old_length * sizeof(Val*));
        if (old_array != this) {
          // redirecting a slice abandons the reference to the array
          assert(old_array->ref() == 1);
          old_array->dec_ref();
        }
      }
      SetRange(0, new_length);
      // TODO: consider adding a "realloc" method to Memory that
      // will split this block to give back the part that we are abandoning
      // here if this was not already a slice
    }
  }
  memmove(dst, src, src_length * sizeof(Val*));
}


//----------------------------------------------------------------------------
// Implementation of BytesVal

void BytesVal::PutSlice(Proc* proc, int beg, int end, BytesVal* x) {
  assert(ref_ >= 0);
  assert(x->ref_ >= 0);
  assert(is_unique());  // must have been uniq'ed
  assert(beg == 0 || legal_index(beg - 1));
  assert(end == 0 || legal_index(end - 1));
  char* dst = base() + beg;
  int dst_length = end - beg;
  char* src = x->base();
  int src_length = x->length();

  if (dst_length == src_length) {
    // replacing a slice with a bytes value of the same length
    // => simply replace the old one with the new one; no need to call SetRange
    if (dst_length == 1) {
      *dst = *src;
      return;
    }
  } else {
    // since we have Uniq'd, if we have a slice we can use the entire original
    int old_length = length();
    int avail_length = array_->length() - origin();
    int new_length = old_length + src_length - dst_length;
    if (new_length <= avail_length) {
      // fits, just reposition the suffix before copying the assigned slice
      if (end != old_length)
        memmove(dst + src_length, dst + dst_length, old_length - end);
      SetRange(origin(), new_length);
    } else {
      // does not fit; if it is a slice we might still have enough space
      // if we relocate the origin to zero; otherwise allocate a new value
      char* dst_old_base = base();
      BytesVal* old_array = array_;
      if (array_ == this || new_length > array_->length()) {
        // allocate a new bytes value and copy the prefix and suffix
        // make "this" a slice that refers to the new value
        // form_, ref_, size_ remain unchanged
        array_ = Factory::NewBytes(proc, new_length);
      }
      char* dst_new_base = array_->base();
      if (beg != 0)
        memmove(dst_new_base, dst_old_base, beg);
      dst = dst_new_base + beg;
      if (end != old_length)
        memmove(dst + src_length, dst_old_base + end, old_length - end);
      if (old_array != this && old_array != array_)
        old_array->dec_ref();  // deferred until data was copied
      SetRange(0, new_length);
      // TODO: consider adding a "realloc" method to Memory that
      // will split this block to give back the part that we are abandoning
      // here if this was not already a slice
    }
  }
  memmove(dst, src, src_length);
}


//----------------------------------------------------------------------------
// Implementation of StringVal

// Used only by is_ascii(); not needed to be set up.
OffsetMap StringVal::ASCIIMap;


void StringVal::SetRange(Proc* proc, int origin, int length, int num_runes) {
  assert(ref_ >= 0);
  assert(!is_readonly());
  assert(num_runes >= 0 && num_runes <= length);
  assert(origin >= 0);
  assert(length >= 0);
  assert(is_slice() || length <= size());
  if (is_slice()) {
    slice_.origin = origin;
  } else {
    assert(origin == 0);
  }
  length_ = length;
  num_runes_ = num_runes;
  if (length == num_runes) {
    // Free the old map, if allocated.
    if (map_ != NULL && map_ != &StringVal::ASCIIMap)
      Deallocate(proc, map_);
    map_ = &ASCIIMap;
  } else if (map_ == &ASCIIMap) {
    // StringVals allocated from persistent Proc
    // memory must never lose their map and thus
    // should never reach here
    // TODO: argue why this is correct (static initialization
    // code may be running for quite some time).
    assert((proc->mode() & Proc::kPersistent) == 0);
    map_ = NULL;
  } else if (map_ != NULL) {
    map_->Reset();
  }
}


void StringVal::SetSubrange(Proc* proc, int origin, int length, int num_runes) {
  SetRange(proc, this->origin() + origin, length, num_runes);
}


szl_int StringVal::byte_offset(Proc* proc, szl_int rune_index) {
  assert(ref_ >= 0);
  if (is_ascii())
    return rune_index;
  if (map_ == NULL) {
    assert(!is_readonly());
    map_ = OffsetMap::New(proc);
  }
  return map_->byte_offset(this, rune_index);
}


Rune StringVal::at(int byte_offset) {
  assert(ref_ >= 0);
  assert(legal_index(byte_offset));
  Rune r;
  FastCharToRune(&r, base() + byte_offset);
  assert(r != 0);
  return r;
}


void StringVal::put(Proc* proc, int byte_offset, Rune src_rune) {
  assert(ref_ >= 0);
  assert(is_unique());  // must have been uniq'ed
  assert(legal_index(byte_offset));
  assert(src_rune != 0);      // don't allow nul chars
  char* dst = base() + byte_offset;
  Rune dst_rune;               // value being replaced (not used)
  int dst_size = FastCharToRune(&dst_rune, dst);
  char src[UTFmax];
  int src_size = runetochar(src, &src_rune);
  if (src_size == dst_size) {
    // both characters have the equal-sized UTF8 encoding
    // => simply replace the old one with the new one; no need to call SetRange
    if (dst_size == 1) {
      *dst = src_rune;
    } else {
      assert(legal_index(byte_offset + src_size - 1));
      memmove(dst, src, src_size);
    }
  } else {
    PutSliceImpl(proc, byte_offset, dst_size, 1, src, src_size, 1);
  }
}


void StringVal::PutSlice(Proc* proc, int beg, int end, StringVal* x) {
  assert(ref_ >= 0);
  assert(x->ref_ >= 0);
  // beg and end are Rune indexes; we need to convert them to byte offsets
  int dst_offset = byte_offset(proc, beg);
  int dst_length = byte_offset(proc, end) - dst_offset;
  int num_runes = end - beg;
  PutSliceImpl(proc, dst_offset, dst_length, num_runes,
               x->base(), x->length(), x->num_runes());
}


void StringVal::PutSliceImpl(Proc* proc, int dst_offset, int dst_size,
                 int dst_runes, char* src, int src_size, int src_runes) {
  assert(ref_ >= 0);
  assert(is_unique());  // must have been uniq'ed
  assert(dst_offset == 0 || legal_index(dst_offset - 1));
  assert(dst_size == 0 || legal_index(dst_offset + dst_size - 1));
  char* dst = base() + dst_offset;

  if (dst_size == src_size) {
    // replacing a slice with a string value of the same byte length
    // => simply replace the old one with the new one
    if (dst_size == 1) {
      *dst = *src;
      return;  // rune offsets did not change, so need not call SetRange
    }
    // call SetRange if rune offsets could have changed, to reset the OffsetMap
    if (dst_runes != 1 || src_runes != 1)
      SetRange(proc, origin(), length(), num_runes_ + src_runes - dst_runes);
  } else {
    // since we have Uniq'd, if we have a slice we can use the entire original
    int old_length = length();
    int avail_size = is_slice() ? (array()->size() - origin()) : size();
    int new_size = old_length + src_size - dst_size;
    if (new_size <= avail_size) {
      // fits, just reposition the suffix before copying the the assigned slice
      int suffix_size = old_length - (dst_offset + dst_size);
      if (suffix_size != 0)
        memmove(dst + src_size, dst + dst_size, suffix_size);
    } else {
      // does not fit; if it is a slice we might still have enough space
      // if we relocate the origin to zero; otherwise allocate a new value
      StringVal* dst_array;
      char* dst_old_base = base();
      int suffix_size = old_length - (dst_offset + dst_size);
      if (!is_slice() || new_size > array()->size()) {
        // allocate a new string and copy the prefix and suffix
        // make "this" a slice that refers to the new value
        // form_, ref_, size_ remain unchanged
        if (!is_slice())
          assert(size_ >= sizeof(SliceInfo));
        dst_array  = Factory::NewString(proc, new_size,
                                        num_runes_ + src_runes - dst_runes);
      } else {
        dst_array = slice_.array;
      }
      char* dst_new_base = dst_array->base();
      if (dst_offset != 0)
        memmove(dst_new_base, dst_old_base, dst_offset);
      dst = dst_new_base + dst_offset;
      if (suffix_size != 0)
        memmove(dst + src_size, dst_old_base + dst_offset + dst_size,
                suffix_size);
      // note that setting "slice_" overwrites the initial characters of the
      // value if it was not already a slice; OK do to this now since any
      // copying from the original value is done and any overlap with "src"
      // will have been removed by the uniqueness check.
      if (is_slice() && slice_.array != dst_array)
        slice_.array->dec_ref();  // deferred until data was copied
      size_ = -1;
      slice_.origin = 0;
      slice_.array = dst_array;
      // TODO: consider adding a "realloc" method to Memory that
      // will split this block to give back the part that we are abandoning
      // here if this was not already a slice
    }
    SetRange(proc, origin(), new_size, num_runes_ + src_runes - dst_runes);
  }
  memmove(dst, src, src_size);
}


string StringVal::cpp_str(Proc* proc) {
  return string(base(), length());
}


// No attempt to guarantee valid UTF-8 at end of buf; TODO?
char* StringVal::c_str(char* buf, int nbuf) {
  assert(ref_ >= 0);
  int len = nbuf - 1;  // Bytes before \0
  if (len > length())
    len = length();
  memmove(buf, base(), len);
  buf[len] = '\0';
  return buf;
}


void StringVal::AllocateOffsetMap(Proc* proc) {
  // force allocation of a map or use the ASCII map
  byte_offset(proc, 0);
}


//----------------------------------------------------------------------------
// Implementation of MapVal

Val* MapVal::Fetch(Val* key) {
  assert(ref_ >= 0);
  assert(!is_ptr() || ref_ >= 0);
  int32 index = map_->Lookup(key);
  if (index == -1)
    return NULL;
  return map_->Fetch(index);
}


void MapVal::Insert(Proc* proc, Val* key, Val* value) {
  assert(ref_ >= 0);
  assert(!is_ptr() || ref_ >= 0);
  map_->SetValue(map_->InsertKey(key), value);
}


void MapVal::InitMap(Proc* proc, int occupancy, bool exact) {
  assert(ref_ >= 0);
  map_ = Map::MakeMapMem(proc, occupancy, exact);
  map_->Init();
}


//----------------------------------------------------------------------------
// Implementation of ClosureVal

int ClosureVal::dynamic_level(Proc* proc) const {
  // Counts number of dynamic frames starting with the closure's context.
  assert(ref_ >= 0);
  int level;
  if ((proc->mode() & Proc::kNative) == 0) {
    // static = 1, global non-static = 2, etc.
    level = 0;
    for (Frame* fp = context_; fp != NULL; fp = fp->dynamic_link())
      level++;
  } else {
    // static functions are all at dynamic level 1
    if (context_ == proc->state_.gp_) {
      level = 1;
    } else {
      // for non-static functions count the dynamic frames until global scope
      level = 2;  // global scope is level 2
      NFrame* bottom_sp = reinterpret_cast<NFrame*>(proc->native_bottom_sp());
      for (NFrame* nfp = reinterpret_cast<NFrame*>(context_);
           nfp < bottom_sp;
           nfp = nfp->dynamic_link()) {
        assert(nfp != NULL);
        level++;
      }
    }
  }
  return level;
}


}  // namespace sawzall
