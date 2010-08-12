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

namespace sawzall {

// The base class for all objects
class Val;

// The TaggedInts class implements operations for tagged pointers/integers:
// Because pointers are usually aligned, some of the least significant bits of a
// pointer are always 0. We encode small integers (smi's) as "pointers" with
// one of the least significant bits set to 1. This way, smi's and other objects
// can be freely mixed and encoded as a single pointer.

class TaggedInts {
 public:
  static const int nptr_bits = sizeof(void*) * 8;
  static const int ntag_bits = 2;  // we assume pointers are 4-byte aligned
  static const int nsmi_bits = nptr_bits - ntag_bits;

  static const intptr_t tag_mask = (static_cast<intptr_t>(1) << ntag_bits) - 1;
  static const intptr_t smi_mask = (static_cast<intptr_t>(1) << nsmi_bits) - 1;

  static const bool ptr_tag = false;
  static const bool smi_tag = true;


  // --------------------------------------------------------------------------
  // types
  typedef intptr_t smi;


  // --------------------------------------------------------------------------
  // testers

  static inline bool is_null(const Val* x) {
    assert(ptr_tag == 0);
    return x == NULL;
  }

  static inline bool is_zero(const Val* x) {
    return x == MakeVal(0);
  }

  static inline bool is_ptr(const Val* x) {
    return (reinterpret_cast<smi>(x) & tag_mask) == ptr_tag;
  }

  static inline bool is_smi(const Val* x) {
    return (reinterpret_cast<smi>(x) & tag_mask) == smi_tag;
  }

  static inline bool fits_smi(szl_int x) {
    assert((-1 >> ntag_bits) == -1);  // >> is arithmetic shift
    return ((static_cast<smi>(x) << ntag_bits) >> ntag_bits) == x;
  }


  // --------------------------------------------------------------------------
  // creation

  static inline Val* MakeVal(szl_int x) {
    assert(fits_smi(x));
    return reinterpret_cast<Val*>((static_cast<smi>(x) << ntag_bits) | smi_tag);
  }


  // --------------------------------------------------------------------------
  // conversions

  static inline smi as_smi(const Val* x) {
    assert(is_smi(x));
    assert((-1 >> ntag_bits) == -1);  // >> is arithmetic shift
    return reinterpret_cast<smi>(x) >> ntag_bits;
  }

  static szl_int as_int(Val* x) {
    return is_smi(x) ? as_smi(x) : as_int_internal(x);
  }

  static Val* as_Val(Proc* proc, szl_int x) {
    return fits_smi(x) ? MakeVal(x) : as_Val_internal(proc, x);
  }


  // --------------------------------------------------------------------------
  // arithmetics

  static inline void Inc(Proc* proc, Val** x, szl_int delta) {
    // TODO: optimize this code
    *x = as_Val(proc, as_int(*x) + delta);
  }

  static inline Val* Add(Proc* proc, Val* x, Val* y) {
#ifndef _LP64
    // smis can only be added directly if sizeof(smi) < sizeof(szl_int)
    szl_int r = (szl_int)(smi)(x) + (szl_int)(smi)(y);
    smi s = (smi)r;
    if ((s & tag_mask) == (2 * smi_tag) && s == r)
      return (Val*)(s - smi_tag);
#endif
    // slow case
    return as_Val(proc, as_int(x) + as_int(y));
  }

  static inline Val* Sub(Proc* proc, Val* x, Val* y) {
    if (is_smi(x) && is_smi(y)) {
      smi r = as_smi(x) - as_smi(y);
      if (fits_smi(r))
        return MakeVal(r);
    }
    // slow case
    return as_Val(proc, as_int(x) - as_int(y));
  }

  static inline Val* Mul(Proc* proc, Val* x, Val* y) {
    // TODO: optimized case here
    // slow case
    return as_Val(proc, as_int(x) * as_int(y));
  }

  static inline Val* Div(Proc* proc, Val* x, Val* y) {
    if (is_zero(y))
      return NULL;
    // TODO: optimized case here
    // slow case
    return as_Val(proc, as_int(x) / as_int(y));
  }

  static inline Val* Rem(Proc* proc, Val* x, Val* y) {
    // TODO: should we only allow positive remainders
    if (is_zero(y))
      return NULL;
    // TODO: optimized case here
    // slow case
    return as_Val(proc, as_int(x) % as_int(y));
  }

  // --------------------------------------------------------------------------
  // comparisons

  static inline bool Lss(Val* x, Val* y) {
    if (is_smi(x) && is_smi(y))
      return reinterpret_cast<smi>(x) < reinterpret_cast<smi>(y);
    // slow case
    return as_int(x) < as_int(y);
  }

 private:
   static szl_int as_int_internal(Val* x);
   static Val* as_Val_internal(Proc* proc, szl_int x);
};

}  // namespace sawzall
