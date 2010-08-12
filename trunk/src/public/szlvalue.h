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

// This is one of a small number of top-level header files for the Sawzall
// component.  See sawzall.h for a complete list.  No other lower-level
// header files should be included by clients of the Sawzall implementation.

#ifndef _PUBLIC_SZLVALUE_H__
#define _PUBLIC_SZLVALUE_H__

#include <string>

#include "public/szltype.h"


class SzlType;
class SzlEncoder;
class SzlDecoder;

// A Sawzall output value, as described in a SzlType object.

union SzlValue {
  int64 i;      // Basic types are stored directly.
  double f;

  // This is the string rep for STRINGs and BYTES,
  // or the flattened rep of basic values for tuples,
  // or the key/value pairs for maps (i.e., elements
  // at even indices are keys and the other elements
  // are values: { key, value, key, value, etc. }
  struct {
    union {
      // String or bytes.
      char* buf;

      // Tuples: flattened basic values and/or map values
      // Maps: (key, value) pairs stored sequentially.
      SzlValue* vals;
    };
    int len;
  } s;

  SzlValue() {
    s.buf = NULL;
    s.vals = NULL;
    s.len = 0;
  }
  explicit SzlValue(int32 ai): i(ai) {}
  explicit SzlValue(uint32 ai): i(ai) {}
  explicit SzlValue(int64 ai): i(ai) {}
  explicit SzlValue(uint64 au): i(static_cast<int64>(au)) {}
  explicit SzlValue(double af): f(af) {}
};

// Set of operations on szl values of a specific type.
// Operations which overwrite values properly allocate and deallocate memory.
class SzlOps {
 public:
  SzlOps(const SzlType& type);
  SzlOps(const SzlOps& ops);
  void operator=(const SzlOps&);
  ~SzlOps();

  // Retrieve the type
  const SzlType& type() const { return type_; }

  // Return the number of values when flattened.
  int nflats() const { return nflats_; }
  const SzlType::Kind& kind(int i) const { return flats_[i]; }

  // Some methods are marked CRITICAL PATH -- they have been determined to
  // take a significant time in certain emit-heavy jobs.

  // Return the amount of memory used to store s. CRITICAL PATH.
  int Memory(const SzlValue& s) const {
    switch (type_.kind()) {
      case SzlType::BOOL:
      case SzlType::FINGERPRINT:
      case SzlType::INT:
      case SzlType::TIME:
      case SzlType::FLOAT:
        return sizeof(SzlValue);
      case SzlType::STRING:
      case SzlType::BYTES:
        return sizeof(SzlValue) + s.s.len;
      default:
        return MemoryInternal(s);
    }
  }

  // Clear the value, freeing any associated storage. Does *not* assign
  // zero to simple types.  Must be called to properly dispose of a SzlValue's
  // storage. CRITICAL PATH.
  void Clear(SzlValue* val) const {
    switch (type_.kind()) {
      case SzlType::BOOL:
      case SzlType::FINGERPRINT:
      case SzlType::INT:
      case SzlType::TIME:
      case SzlType::FLOAT:
        break;
      case SzlType::STRING:
      case SzlType::BYTES:
        delete[] val->s.buf;
        val->s.buf = NULL;
        val->s.len = 0;
        break;
      default:
        ClearInternal(val);
    }
  }

  // Sets the value to zero. If the value is a tuple, all fields are zero-ed.
  // Complex types such as maps and arrays are cleared and become empty.
  void AssignZero(SzlValue* val) const;

  // d = s; takes care of memory allocation.
  void Assign(const SzlValue& s, SzlValue* d) const;

  // d = s at the given flattened position pos or for the specified range;
  // takes care of memory allocation.
  // REQUIRES: 0 <= start < end <= s.s.len
  void AssignRange(const SzlValue& s, int start, int end, SzlValue* d) const;
  // REQUIRES: 0 <= pos < s.s.len
  void AssignAtPos(const SzlValue& s, int pos, SzlValue* d) const;

  // Put each basic type at the given flattened position.
  // REQUIRES: pos >= 0, pos < nflats()
  void PutBool(bool b, int pos, SzlValue* d) const;
  void PutBytes(const char* s, int len, int pos, SzlValue* d) const;
  void PutFingerprint(uint64 fp, int pos, SzlValue* d) const;
  void PutFloat(double f, int pos, SzlValue* d) const;
  void PutInt(int64 i, int pos, SzlValue* d) const;
  void PutString(const char* s, int len, int pos, SzlValue* d) const;
  void PutTime(uint64 t, int pos, SzlValue* d) const;

  // Append SzlEncoder rep of s to out.
  void AppendToString(const SzlValue& s, string* out) const;

  // Parse from a SzlEncoder rep.
  bool ParseFromArray(const char* buf, int len, SzlValue* val) const;

  // Encode a value to enc.
  void Encode(const SzlValue& v, SzlEncoder* enc) const;

  // Decode a value from dec.
  bool Decode(SzlDecoder* dec, SzlValue* val) const;

  // Skip the value in dec.  Returns whether the value had the correct form.
  bool Skip(SzlDecoder* dec) const;

  // Generic comparator: s0 < s1 -> -1, s0 == s1 -> 0, s0 > s1 -> +1
  // CRITICAL PATH
  int Cmp(const SzlValue* s0, const SzlValue* s1) const {
    switch (type_.kind()) {
      case SzlType::INT:
        return CmpBaseT(s0 == NULL ? 0 : s0->i,
                        s1 == NULL ? 0 : s1->i);
      case SzlType::BOOL:
      case SzlType::FINGERPRINT:
      case SzlType::TIME:
        return CmpBaseT(s0 == NULL ? 0ULL : static_cast<uint64>(s0->i),
                        s1 == NULL ? 0ULL : static_cast<uint64>(s1->i));
      case SzlType::FLOAT:
        return CmpBaseT(s0 == NULL ? 0.0 : s0->f,
                        s1 == NULL ? 0.0 : s1->f);
      case SzlType::STRING:
      case SzlType::BYTES:
        return CmpStr(s0, s1);
      default:
        return CmpInternal(s0, s1);
    }
  }

  // s0 == s1.
  bool Eq(const SzlValue&s0, const SzlValue& s1) const {
    return (Cmp(&s0, &s1) == 0);
  }

  // Restricted operations: REQUIRES IsOrdered()

  // Check if values are ordered.
  // Currently true for all base types and their tuples.
  static bool IsOrdered(const SzlType& type);

  // s0 < s1
  // REQUIRES: IsOrdered. CRITICAL PATH.
  bool Less(const SzlValue& s0, const SzlValue& s1) const {
    return (Cmp(&s0, &s1) < 0);
  }

  // is s0 < s1 at flattened tuple position pos?
  // REQUIRES: IsOrdered. CRITICAL PATH.
  bool LessAtPos(const SzlValue& s0, int pos, const SzlValue& s1) const;


  // Restricted operations: REQUIRES IsAddable()

  // Check if values can be Added, Subtracted, and Negated.
  // True for TIME, INT, FLOAT, and their tuples and maps.
  static bool IsAddable(const SzlType& type);

  // d = -s
  // REQUIRES: IsAddable. CRITICAL PATH.
  void Negate(const SzlValue&s, SzlValue* d) const {
    switch (type_.kind()) {
      case SzlType::BOOL:
      case SzlType::FINGERPRINT:
      case SzlType::INT:
      case SzlType::TIME:
        d->i = -s.i; break;
      case SzlType::FLOAT:
        d->f = -s.f; break;
      default:
        NegateInternal(s, d);
    }
  }

  // d += s
  // REQUIRES: IsAddable. CRITICAL PATH.
  void Add(const SzlValue& s, SzlValue* d) const {
    switch (type_.kind()) {
      case SzlType::BOOL:
      case SzlType::FINGERPRINT:
      case SzlType::INT:
      case SzlType::TIME:
        d->i += s.i; break;
      case SzlType::FLOAT:
        d->f += s.f; break;
      default: AddInternal(s, d);
    }
  }

  // d -= s
  // REQUIRES: IsAddable
  void Sub(const SzlValue& s, SzlValue* d) const;


  // Restricted operations: REQUIRS IsNumeric

  // Check if values can be multiplied, divided, and converted to Float.
  // True for INT, FLOAT, and their tuples.
  static bool IsNumeric(const SzlType& type);

  // Cast every value to a double
  // REQUIRES: IsNumeric
  void ToFloat(const SzlValue& s, double* floats) const;


  // Helpers for debugging.

  // Returns whether the type is complex, i.e., whether it contains
  // embedded SzlOps objects or not.
  bool IsComplex() const;

 private:

  // Helper to initializes state when created or copied.
  void Init();
  void Delete();
  void EncodeDefault(SzlEncoder* enc) const;
  void EncodeInternal(const SzlValue& v, SzlEncoder* enc, bool top_level) const;
  bool DecodeInternal(SzlDecoder* dec, SzlValue* val, bool top_level) const;
  bool SkipInternal(SzlDecoder* dec, bool top_level) const;

  // Little helper template for comparing a and b. The result
  // is 0 if a == b, -1 if a < b and +1 if a > b.
  template <typename T>
  static inline int CmpBaseT(T a, T b) {
    if (a < b) return -1;
    else if (a > b) return 1;
    return 0;
  }

  // Complex ops, for cases not inlined above.
  int MemoryInternal(const SzlValue& s) const;
  void ClearInternal(SzlValue* val) const;
  int CmpInternal(const SzlValue* s0, const SzlValue* s1) const;
  static int CmpStr(const SzlValue* s0, const SzlValue* s1);
  static int CmpBase(SzlType::Kind k, const SzlValue* s0, const SzlValue* s1);
  void AddInternal(const SzlValue& s, SzlValue* d) const ;
  void NegateInternal(const SzlValue& s, SzlValue* d) const;

  // Gets the SzlValue at the specified position and makes sure the kind
  // is correct.
  SzlValue* SzlFlatValueAt(int pos,
                           SzlValue* v,
                           SzlType::Kind expected_kind) const;

  SzlType type_;

  int nflats_;                  // number of values in the flattened rep.
  SzlType::Kind* flats_;        // kinds of the basic types in flattened rep.

  // To support maps (and potentially other complex types), which cannot
  // be flattened, we store SzlOps objects to take care of the embedded
  // structures. This array will be NULL if there are no embedded structures
  // and if not NULL, then only complex types will have a non-NULL entry in
  // the array.
  SzlOps** flat_ops_;
};

// Comparison classes
class SzlValueCmp {
 public:
  virtual bool Cmp(const SzlValue& v1, const SzlValue& v2) const = 0;
  virtual ~SzlValueCmp();
};

class SzlValueLess: public SzlValueCmp {
 public:
  SzlValueLess(const SzlOps* ops);
  virtual bool Cmp(const SzlValue& v1, const SzlValue& v2) const;

 private:
  const SzlOps* ops_;
};

class SzlValueGreater: public SzlValueCmp {
 public:
  SzlValueGreater(const SzlOps* ops);
  virtual bool Cmp(const SzlValue& v1, const SzlValue& v2) const;

 private:
  const SzlOps* ops_;
};

#endif  // _PUBLIC_SZLVALUE_H__
