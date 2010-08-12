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
#include <algorithm>
#include <string>
#include <vector>

#include "public/porting.h"
#include "public/logging.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlvalue.h"

SzlValueCmp::~SzlValueCmp() {
}

SzlValueLess::SzlValueLess(const SzlOps* ops): ops_(ops) {
}

bool SzlValueLess::Cmp(const SzlValue& v1, const SzlValue& v2) const {
  return ops_->Less(v1, v2);
}

SzlValueGreater::SzlValueGreater(const SzlOps* ops): ops_(ops) {
}

bool SzlValueGreater::Cmp(const SzlValue& v1, const SzlValue& v2) const {
  return ops_->Less(v2, v1);
}

// A little helper function that returns whether the specified kind corresponds
// to a "simple base" kind, which are all base kinds except for strings and
// bytes.
static inline bool IsSimpleBaseKind(SzlType::Kind kind) {
  return SzlType::BaseKind(kind) && kind != SzlType::STRING &&
      kind != SzlType::BYTES;
}

// Check if values are ordered.
// True for base types, and tuples thereof.
bool SzlOps::IsOrdered(const SzlType& t) {
  switch (t.kind()) {
    case SzlType::BOOL:
    case SzlType::BYTES:
    case SzlType::STRING:
    case SzlType::TIME:
    case SzlType::INT:
    case SzlType::FINGERPRINT:
    case SzlType::FLOAT:
      return true;

    case SzlType::TUPLE:
      for (int i = 0; i < t.fields_size(); ++i)
        if (!IsOrdered(t.field(i).type()))
          return false;
      return true;

    default:
      return false;
  }
}

// Check if values can be Added, Subtracted, and Negated.
// True for TIME, INT, FLOAT, and tuples and maps thereof.
bool SzlOps::IsAddable(const SzlType& t) {
  switch (t.kind()) {
    case SzlType::TIME:
    case SzlType::INT:
    case SzlType::FLOAT:
      return true;

    case SzlType::TUPLE:
      for (int i = 0; i < t.fields_size(); ++i)
        if (!IsAddable(t.field(i).type()))
          return false;
      return true;

    case SzlType::MAP:
      if (!IsAddable(t.element()->type()))
          return false;
      return true;

    default:
      return false;
  }
}

// Check if can be multiplied, divided, and converted to Float.
// True for INT, FLOAT, and tuples thereof.
bool SzlOps::IsNumeric(const SzlType& t) {
  switch (t.kind()) {
    case SzlType::INT:
    case SzlType::FLOAT:
      return true;

    case SzlType::TUPLE:
      for (int i = 0; i < t.fields_size(); ++i)
        if (!IsNumeric(t.field(i).type()))
          return false;
      return true;

    default:
      return false;
  }
}

bool SzlOps::IsComplex() const {
  return flat_ops_ != NULL;
}

// Count the number of elements in the flattened rep. The argument "is_complex"
// will be set to "true" for hierarchical structures, such as tuples with a
// map or array field, or maps/arrays with. Note that a map itself is only
// complex if either the index or element (or both) are non-base types. However,
// a tuple of a map (even a "simple" map) is always complex.
// Note: is_complex must be initialized to "false" before calling this function.
static int SzlFlattenedVals(int depth, const SzlType& t, bool* is_complex) {
  if (t.BaseType()) {
    return 1;
  } else if (t.kind() == SzlType::MAP) {
    // If either the index or element type is a non-base type or the depth
    // is non-zero (= embedded map), then this map is complex.
    if (!t.index(0).type().BaseType() || !t.element()->type().BaseType() ||
        depth > 0) {
      *is_complex = true;
    }
    return 1;
  } else if (t.kind() == SzlType::ARRAY) {
    // If either element type is a non-base type or the depth
    // is non-zero (= embedded array), then this map is complex.
    if (!t.element()->type().BaseType() || depth > 0) {
      *is_complex = true;
    }
    return 1;
  } else if (t.kind() == SzlType::TUPLE) {
    int n = 0;
    for (int i = 0; i < t.fields_size(); ++i)
      n += SzlFlattenedVals(depth + 1, t.field(i).type(), is_complex);
    return n;
  } else {
    LOG(FATAL) << "can't perform ops on " << t;
  }
  return 0;  // Shouldn't get here.
}

// Create the flattened type array.
static void SzlFlattenKinds(const SzlType& t, SzlType::Kind* flats,
                            SzlOps** flat_ops, int* iota) {
  if (t.BaseType()) {
    flats[*iota] = t.kind();
    ++*iota;
  } else if (t.kind() == SzlType::MAP || t.kind() == SzlType::ARRAY) {
    CHECK(flat_ops != NULL);
    flats[*iota] = t.kind();
    flat_ops[*iota] = new SzlOps(t);
    ++*iota;
  } else if (t.kind() == SzlType::TUPLE) {
    for (int i = 0; i < t.fields_size(); ++i) {
      SzlFlattenKinds(t.field(i).type(), flats, flat_ops, iota);
    }
  } else {
    LOG(FATAL) << "can't perform ops on " << t;
  }
}

void SzlOps::Init() {
  // Maps and arrays are not type-flattened, hence special cases.
  if (type_.kind() == SzlType::MAP) {
    if (type_.indices_size() != 1) {
      LOG(FATAL) << "maps with multiple keys are not supported";
      return;
    }
    const SzlType& index_type = type_.index(0).type();
    const SzlType& element_type = type_.element()->type();

    nflats_ = 2;  // 1 key type and 1 value type.
    flats_ = new SzlType::Kind[nflats_];
    flats_[0] = index_type.kind();
    flats_[1] = element_type.kind();

    // If the index or element is not a base type, then we're
    // dealing with a hierarchy.
    if (!index_type.BaseType() || !element_type.BaseType()) {
      flat_ops_ = new SzlOps*[nflats_];
      flat_ops_[0] = index_type.BaseType() ? NULL : new SzlOps(index_type);
      flat_ops_[1] = element_type.BaseType() ? NULL : new SzlOps(element_type);
    }
  } else if (type_.kind() == SzlType::ARRAY) {
    const SzlType& element_type = type_.element()->type();

    nflats_ = 1;  // 1 value type.
    flats_ = new SzlType::Kind[nflats_];
    flats_[0] = element_type.kind();

    // If the element is not a base type, then we're dealing with a hierarchy.
    if (!element_type.BaseType()) {
      flat_ops_ = new SzlOps*[nflats_];
      flat_ops_[0] = element_type.BaseType() ? NULL : new SzlOps(element_type);
    }
  } else {
    bool is_complex = false;  // Must be initialized to "false".
    nflats_ = SzlFlattenedVals(0, type_, &is_complex);
    flats_ = new SzlType::Kind[nflats_];

    // For complex structures we need to use embedded SzlOps for
    // certain values.
    if (is_complex) {
      flat_ops_ = new SzlOps*[nflats_];
      memset(flat_ops_, 0, sizeof(flat_ops_) * nflats_);
    }
    int n = 0;
    SzlFlattenKinds(type_, flats_, flat_ops_, &n);
    CHECK_EQ(nflats_, n);
  }
}

SzlOps::SzlOps(const SzlType& type): type_(type), flat_ops_(NULL) {
  Init();
}

SzlOps::SzlOps(const SzlOps& ops): type_(ops.type_), flat_ops_(NULL) {
  Init();
}

void SzlOps::operator=(const SzlOps& ops) {
  // Check for self-assignment.
  if (&ops == this)
    return;

  // Delete old memory and re-initialize based on the new type.
  Delete();
  type_ = ops.type_;
  Init();
}

void SzlOps::Delete() {
  delete[] flats_;

  if (flat_ops_ != NULL) {
    for (int i = 0; i < nflats_; ++i) {
      delete flat_ops_[i];
    }
    delete[] flat_ops_;
  }
}

SzlOps::~SzlOps() {
  Delete();
}

// Return the amount of memory used to store s, nontrivial cases
int SzlOps::MemoryInternal(const SzlValue& s) const {
  switch (type_.kind()) {
    case SzlType::TUPLE:
    case SzlType::ARRAY:
    case SzlType::MAP: {
        int mem = sizeof(SzlValue);
        if (s.s.len) {
          assert(type_.kind() != SzlType::TUPLE || s.s.len == nflats_);
          mem += s.s.len * sizeof(SzlValue);
          for (int i = 0; i < s.s.len; ++i) {
            int flat_i = i % nflats_;
            SzlType::Kind k = flats_[flat_i];
            if (k == SzlType::STRING || k == SzlType::BYTES) {
              mem += s.s.vals[i].s.len;
            } else if (flat_ops_ != NULL && flat_ops_[flat_i] != NULL) {
              // Need to subtract space for the type itself, since that is
              // already accounted for. Hence "- sizeof(SzlValue)".
              mem += flat_ops_[flat_i]->Memory(s.s.vals[i]) - sizeof(SzlValue);
            } else if (!IsSimpleBaseKind(k)) {
              LOG(FATAL) << "can't report memory usage for " << type_;
              return 0;
            }
          }
        }
        return mem;
      }
    default:
      LOG(FATAL) << "can't report memory usage for " << type_;
      return 0;
  }
}

void SzlOps::ClearInternal(SzlValue* val) const {
  switch (type_.kind()) {
    case SzlType::TUPLE:
    case SzlType::ARRAY:
    case SzlType::MAP:
      if (val->s.len) {
        assert(type_.kind() != SzlType::TUPLE || val->s.len == nflats_);
        for (int i = 0; i < val->s.len; ++i) {
          int flat_i = i % nflats_;
          SzlType::Kind k = flats_[flat_i];
          if (k == SzlType::STRING || k == SzlType::BYTES) {
            delete[] val->s.vals[i].s.buf;
            val->s.vals[i].s.buf = NULL;
            val->s.vals[i].s.len = 0;
          } else if (flat_ops_ != NULL && flat_ops_[flat_i] != NULL) {
            flat_ops_[flat_i]->Clear(&(val->s.vals[i]));
          } else if (!IsSimpleBaseKind(k)) {
            LOG(FATAL) << "can't clear for " << type_;
            return;
          }
        }
        delete[] val->s.vals;
        val->s.vals = NULL;
        val->s.len = 0;
      }
      break;
    default:
      LOG(FATAL) << "can't clear for " << type_;
      return;
  }
}

void SzlOps::AssignZero(SzlValue* val) const {
  switch (type_.kind()) {
    case SzlType::BOOL:
    case SzlType::FINGERPRINT:
    case SzlType::INT:
    case SzlType::TIME:
      val->i = 0;
      break;
    case SzlType::FLOAT:
      val->f = 0.0;
      break;
    case SzlType::STRING:
    case SzlType::BYTES:
      delete[] val->s.buf;
      val->s.buf = NULL;
      val->s.len = 0;
      break;
    case SzlType::TUPLE: {
      // We zero each of the tuple fields. Complex fields (maps and array)
      // are cleared (i.e., will become empty maps/arrays).
      SzlValue* vals = val->s.vals;
      if (vals == NULL) {
        vals = new SzlValue[nflats_];
        val->s.vals = vals;
        val->s.len = nflats_;
      } else {
        assert(val->s.len == nflats_);
      }
      for (int i = 0; i < nflats_; ++i) {
        SzlType::Kind k = flats_[i];
        if (flat_ops_ != NULL && flat_ops_[i] != NULL) {
          // Clear embedded structures (maps and arrays).
          flat_ops_[i]->Clear(&(vals[i]));
        } else if (k == SzlType::STRING || k == SzlType::BYTES) {
          delete[] vals[i].s.buf;
          vals[i].s.buf = NULL;
          vals[i].s.len = 0;
        } else if (k == SzlType::FLOAT) {
          vals[i].f = 0.0;
        } else if (IsSimpleBaseKind(k)) {
          vals[i].i = 0;
        } else {
          LOG(FATAL) << "can't assign zero for " << type_;
          return;
        }
      }
      break;
    }
    case SzlType::ARRAY:
    case SzlType::MAP:
      // "Zero" arrays and maps are completely empty, hence we can just clear
      // them.
      Clear(val);
      break;
    default:
      LOG(FATAL) << "can't assign zero for " << type_;
      return;
  }
}

// Cast every value to a double.
// Only defined for INT and FLOAT elements, and tuples thereof.
void SzlOps::ToFloat(const SzlValue& s, double* floats) const {
  switch (type_.kind()) {
    case SzlType::INT:
      floats[0] = s.i;
      break;
    case SzlType::FLOAT:
      floats[0] = s.f;
      break;
    case SzlType::TUPLE:
      if (s.s.len) {
        assert(s.s.len == nflats_);
        for (int i = 0; i < nflats_; ++i) {
          SzlType::Kind k = flats_[i];
          if (k == SzlType::FLOAT) {
            floats[i] = s.s.vals[i].f;
          } else if (k == SzlType::INT) {
            floats[i] = s.s.vals[i].i;
          } else {
            LOG(FATAL) << "can't convert to float for " << type_;
            return;
          }
        }
      } else {
        for (int i = 0; i < nflats_; ++i)
          floats[i] = 0.;
      }
      break;
    default:
      LOG(FATAL) << "can't convert to float for " << type_;
      return;
  }
}

// Replace the string/bytes storage for val with buf
static void ReplaceSzlValueBuf(const char* buf, int len, SzlValue* val) {
  // Make sure we don't smash ourselves.
  if (buf == val->s.buf) {
    CHECK_EQ(len, val->s.len);
    return;
  }
  if (val->s.len != len) {  // Don't want to perform unnecessary reallocs.
    delete[] val->s.buf;
    val->s.len = len;
    if (len == 0) {
      val->s.buf = NULL;
      return;
    }
    val->s.buf = new char[len];
  }
  if (buf != NULL && len > 0) {
    memmove(val->s.buf, buf, len);
  }
}

void SzlOps::AssignRange(const SzlValue& s,
                         int start,
                         int end,
                         SzlValue* d) const {
  if (type_.kind() == SzlType::TUPLE || type_.kind() == SzlType::ARRAY ||
      type_.kind() == SzlType::MAP) {
    if (s.s.len != d->s.len) {
      // If this is a tuple, then either the source or destination is empty.
      CHECK(type_.kind() != SzlType::TUPLE || s.s.len == 0 || d->s.len == 0);
      Clear(d);
    }
    if (s.s.len) {
      if (type_.kind() == SzlType::TUPLE) {
        CHECK_EQ(s.s.len, nflats_);
      }
      CHECK_GE(start, 0);
      CHECK_LE(end, s.s.len);
      CHECK_LE(start, end);
      SzlValue* svals = s.s.vals;
      SzlValue* dvals = d->s.vals;
      if (dvals == NULL) {
        dvals = new SzlValue[s.s.len];
        d->s.vals = dvals;
        d->s.len = s.s.len;
      } else {
        CHECK_EQ(s.s.len, d->s.len);
        if (dvals == svals) {  // Self assignment.
          return;
        }
      }
      for (int i = start; i < end; ++i) {
        int flat_i = i % nflats_;
        SzlType::Kind k = flats_[flat_i];
        if (flat_ops_ != NULL && flat_ops_[flat_i] != NULL) {
          flat_ops_[flat_i]->Assign(svals[i], &(dvals[i]));
        } else if (k == SzlType::STRING || k == SzlType::BYTES) {
          ReplaceSzlValueBuf(svals[i].s.buf, svals[i].s.len, &dvals[i]);
        } else if (k == SzlType::FLOAT) {
          dvals[i].f = svals[i].f;
        } else if (IsSimpleBaseKind(k)) {
          dvals[i].i = svals[i].i;
        } else {
          LOG(FATAL) << "can't assign for " << type_ << " at pos " << i;
          return;
        }
      }
    }
  } else {
    if (start + 1 != end || start != 0) {
      LOG(FATAL) << "can't assign range from " << start << " to "
                 << end << " for " << type_;
      return;
    }
    Assign(s, d);
  }
}

void SzlOps::AssignAtPos(const SzlValue& s, int pos, SzlValue* d) const {
  AssignRange(s, pos, pos + 1, d);
}

// d = s; takes care of memory allocation.
void SzlOps::Assign(const SzlValue& s, SzlValue* d) const {
  switch (type_.kind()) {
    case SzlType::BOOL:
    case SzlType::FINGERPRINT:
    case SzlType::INT:
    case SzlType::TIME:
      d->i = s.i;
      break;
    case SzlType::FLOAT:
      d->f = s.f;
      break;
    case SzlType::STRING:
    case SzlType::BYTES:
      ReplaceSzlValueBuf(s.s.buf, s.s.len, d);
      break;
    case SzlType::TUPLE:
    case SzlType::ARRAY:
    case SzlType::MAP:
      // Self assignment or empty assignment (NULL pointers)?
      if (s.s.vals == d->s.vals) {
        break;
      }
      // Assign the full range.
      AssignRange(s, 0, s.s.len, d);
      break;
    default:
      LOG(FATAL) << "can't assign for " << type_;
      return;
  }
}

SzlValue* SzlOps::SzlFlatValueAt(int pos,
                                 SzlValue* v,
                                 SzlType::Kind expected_kind) const {
  assert(pos >= 0 && pos < nflats_);

  if (flats_[pos] != expected_kind) {
    LOG(FATAL) << "can't get flat value at " << pos << " for " << type_
               << ": expected kind " << expected_kind << " but found "
               << flats_[pos];
    return NULL;
  }
  if (type_.kind() == SzlType::TUPLE) {
    SzlValue* vals = v->s.vals;
    if (vals == NULL) {
      vals = new SzlValue[nflats_];
      v->s.vals = vals;
      v->s.len = nflats_;
    }
    v = &vals[pos];
  } else {
    assert(nflats_ == 1);
  }
  return v;
}

// Put each basic type at the given flattened position.
// REQUIRES: pos >= 0, pos < nflats()
void SzlOps::PutBool(bool b, int pos, SzlValue* d) const {
  d = SzlFlatValueAt(pos, d, SzlType::BOOL);
  d->i = b;
}

void SzlOps::PutBytes(const char* s, int len, int pos, SzlValue* d) const {
  d = SzlFlatValueAt(pos, d, SzlType::BYTES);
  ReplaceSzlValueBuf(s, len, d);
}

void SzlOps::PutFloat(double f, int pos, SzlValue* d) const {
  d = SzlFlatValueAt(pos, d, SzlType::FLOAT);
  d->f = f;
}

void SzlOps::PutInt(int64 i, int pos, SzlValue* d) const {
  d = SzlFlatValueAt(pos, d, SzlType::INT);
  d->i = i;
}

void SzlOps::PutFingerprint(uint64 fp, int pos, SzlValue* d) const {
  d = SzlFlatValueAt(pos, d, SzlType::FINGERPRINT);
  d->i = fp;
}

void SzlOps::PutTime(uint64 t, int pos, SzlValue* d) const {
  d = SzlFlatValueAt(pos, d, SzlType::TIME);
  d->i = t;
}

void SzlOps::PutString(const char* s, int len, int pos, SzlValue* d) const {
  d = SzlFlatValueAt(pos, d, SzlType::STRING);

  // Internally, saw strings must be null terminated,
  // and empty strings must have length 0.
  if (len == 0) {
    delete[] d->s.buf;
    d->s.buf = NULL;
    d->s.len = 0;
  } else {
    ReplaceSzlValueBuf(s, len + 1, d);
  }
}

// d = -s,  nontrivial cases
void SzlOps::NegateInternal(const SzlValue& s, SzlValue* d) const {
  switch (type_.kind()) {
    case SzlType::TUPLE:
    case SzlType::ARRAY:
    case SzlType::MAP:
      if (s.s.len != d->s.len) {
        // If this is a tuple, then either the source or destination is empty.
        CHECK(type_.kind() != SzlType::TUPLE || s.s.len == 0 || d->s.len == 0);
        Clear(d);
      }
      if (s.s.len) {
        SzlValue* dvals = d->s.vals;
        SzlValue* svals = s.s.vals;

        if (dvals == NULL) {
          dvals = new SzlValue[s.s.len];
          d->s.vals = dvals;
          d->s.len = s.s.len;
        } else {
          CHECK(d->s.len == s.s.len);
        }
        // Maps must have 1 key type and 1 value type.
        CHECK(type_.kind() != SzlType::MAP || nflats_ == 2);

        for (int i = 0; i < s.s.len; ++i) {
          int flat_i = i % nflats_;
          SzlType::Kind k = flats_[flat_i];

          // For maps, we only want to negate the values, not the keys!
          // Keys must be copied instead.
          if ((i & 1) == 0 && type_.kind() == SzlType::MAP) {
            // This is a map key! Copy.
            AssignAtPos(s, i, d);
          } else {
            // Map value or tuple / array element.
            if (flat_ops_ != NULL && flat_ops_[flat_i] != NULL) {
              flat_ops_[flat_i]->Negate(svals[i], &(dvals[i]));
            } else if (k == SzlType::FLOAT) {
              dvals[i].f = -s.s.vals[i].f;
            } else if (IsSimpleBaseKind(k)) {
              dvals[i].i = -s.s.vals[i].i;
            } else {
              LOG(FATAL) << "can't negate for " << type_;
              return;
            }
          }
        }
      }
      break;
    default:
      LOG(FATAL) << "can't negate for " << type_;
      return;
  }
}


// Candidate for inlining, needs to be measured.
// static
int SzlOps::CmpStr(const SzlValue* s0, const SzlValue* s1) {
  // Deal with NULL strings. A NULL string and an empty string
  // are equal, but a non-empty string is always more than a
  // NULL/empty string.
  if (s0 == NULL && s1 == NULL) {
    return 0;
  } else if (s0 == NULL) {
    assert(s1 != NULL);
    return CmpBaseT(0, s1->s.len);
  } else if (s1 == NULL) {
    assert(s0 != NULL);
    return CmpBaseT(s0->s.len, 0);
  }
  assert(s0 != NULL);
  assert(s1 != NULL);

  int len = s0->s.len;
  if (len > s1->s.len) {
    len = s1->s.len;
  }
  int c = memcmp(s0->s.buf, s1->s.buf, len);

  if (c == 0) {
    return CmpBaseT(s0->s.len, s1->s.len);
  }
  return c;
}

// Used for comparing flattened fields. It remains to be seen whether it's
// worth looking into type traits for those.
// static
int SzlOps::CmpBase(SzlType::Kind kind,
                    const SzlValue* s0,
                    const SzlValue* s1) {
  switch (kind) {
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
      LOG(FATAL) << "not a base kind: " << kind;
      return false;
  };
}

// Returns -1 if s0 < s1, 0 if s0 == s1 and +1 if s0 > s1 , nontrivial cases
int SzlOps::CmpInternal(const SzlValue* s0, const SzlValue* s1) const {
  switch (type_.kind()) {
    case SzlType::TUPLE:
    case SzlType::ARRAY:
    case SzlType::MAP: {
      const SzlValue* v0 = (s0 != NULL ? s0->s.vals : NULL);
      const SzlValue* v1 = (s1 != NULL ? s1->s.vals : NULL);
      int len0 = (s0 != NULL ? s0->s.len : 0);
      int len1 = (s1 != NULL ? s1->s.len : 0);
      if (len0 == 0 && len1 == 0) {
        return 0;
      }
      int len = nflats_;

      // An empty map/array is always smaller than a non-empty map/array,
      // which is not necessarily the case for tuples.
      if (type_.kind() == SzlType::MAP || type_.kind() == SzlType::ARRAY) {
        if (len0 == 0) {
          assert(len1 > 0);
          return -1;
        } else if (len1 == 0) {
          assert(len0 > 0);
          return 1;
        }
        assert(len0 > 0);
        assert(len1 > 0);
        len = MinInt(len0, len1);
      } else if (type_.kind() == SzlType::TUPLE) {
        assert(len0 == nflats_ || len0 == 0);
        assert(len1 == nflats_ || len1 == 0);
      }
      assert(len > 0);

      // Note: maps are compared on an key-value pair basis. I.e., first key[0]
      // is compared, then value[0], then key[1], etc.
      for (int i = 0; i < len; ++i) {
        int flat_i = i % nflats_;
        int res = 0;
        const SzlValue* v0_i = (len0 != 0 ? v0 + i : NULL);
        const SzlValue* v1_i = (len1 != 0 ? v1 + i : NULL);

        if (flat_ops_ != NULL && flat_ops_[flat_i] != NULL) {
          res = flat_ops_[flat_i]->Cmp(v0_i, v1_i);
        } else {
          res = CmpBase(flats_[flat_i], v0_i, v1_i);
        }
        if (res != 0) {
          return res;
        }
      }
      // If this is a tuple, we're done -- the tuples are the same.
      if (type_.kind() == SzlType::TUPLE) {
        return 0;
      }
      // This is a map or array. One map/array is a superset of the other
      // map/array.
      return CmpBaseT(len0, len1);
    }
    default:
      LOG(FATAL) << "can't compare for " << type_;
      return 0;
  }
}

bool SzlOps::LessAtPos(const SzlValue& s0, int pos, const SzlValue& s1) const {
  if (type_.kind() != SzlType::TUPLE) {
    assert(pos == 0);
    return Less(s0, s1);
  }

  assert(pos < nflats_);
  SzlType::Kind k = flats_[pos];

  // We only support base kinds (thus we only support "simple" tuples).
  if (!SzlType::BaseKind(k)) {
    LOG(FATAL) << "can't compare at position " << pos << " for " << type_;
    return false;
  }

  const SzlValue* v0 = s0.s.vals;
  const SzlValue* v1 = s1.s.vals;
  int len0 = s0.s.len;
  int len1 = s1.s.len;
  if (!len0) {
    if (!len1)
      return false;
    assert(len1 == nflats_);
    if (k == SzlType::STRING || k == SzlType::BYTES) {
      // empty string always less than non-empty string.
      return v1[pos].s.len;
    } else if (k == SzlType::FLOAT) {
      return 0. < v1[pos].f;
    } else if (k == SzlType::INT) {
      return 0 < v1[pos].i;
    } else {
      return 0 < static_cast<uint64>(v1[pos].i);
    }
  } else if (!len1) {
    assert(len0 == nflats_);
    // can never be < empty string or unsigned 0.
    if (k == SzlType::FLOAT) {
      return v0[pos].f < 0.;
    } else if (k == SzlType::INT) {
      return v0[pos].i < 0;
    }
  } else {
    assert(len0 == nflats_);
    assert(len1 == nflats_);
    if (k == SzlType::STRING || k == SzlType::BYTES) {
      return (CmpStr(v0 + pos, v1 + pos) < 0);
    } else if (k == SzlType::FLOAT) {
      return v0[pos].f < v1[pos].f;
    } else if (k == SzlType::INT) {
      return v0[pos].i < v1[pos].i;
    } else {
      return static_cast<uint64>(v0[pos].i) < static_cast<uint64>(v1[pos].i);
    }
  }
  return false;
}

// d += s , nontrivial cases
void SzlOps::AddInternal(const SzlValue& s, SzlValue* d) const {
  switch (type_.kind()) {
    case SzlType::TUPLE:
      if (s.s.len) {
        if (!d->s.len) {
          Assign(s, d);
          return;
        }
        assert(s.s.len == nflats_);
        for (int i = 0; i < nflats_; ++i) {
          SzlType::Kind k = flats_[i];
          if (flat_ops_ != NULL && flat_ops_[i] != NULL) {
            flat_ops_[i]->Add(s.s.vals[i], &(d->s.vals[i]));
          } else if (k == SzlType::FLOAT) {
            d->s.vals[i].f += s.s.vals[i].f;
          } else if (IsSimpleBaseKind(k)) {
            d->s.vals[i].i += s.s.vals[i].i;
          } else {
            LOG(FATAL) << "can't add for " << type_;
            return;
          }
        }
      }
      break;
    case SzlType::MAP:
      if (s.s.len) {
        if (!d->s.len) {
          Assign(s, d);
          return;
        }
        // When maps are emitted, the elements are sorted by key. This
        // allows us to quickly sum two maps. Therefore, our output
        // map should also be sorted again. Our strategy is as follows:
        // - Determine the index of the source and destination elements in
        //   the summed map (sorted order). For example:
        //   { a:0, b:1, e:3 } -> 0, 1, 4
        //   { b:1, c:4 }      -> 1, 2
        // - Add the elements. If the size of the destination map changes,
        //   allocate a new array.
        // Thus, we need to make two passes over the elements, but we
        // only need to compare keys once. There is no real alternative,
        // since we need to know how large the resulting map is going to
        // be before we can start adding elements.
        if (type_.indices_size() != 1) {
          LOG(FATAL) << "maps with multiple keys are not supported";
          return;
        }
        if (nflats_ != 2) {
          LOG(FATAL) << "unexpected number of key/value types ("
                     << nflats_ << ")";
          return;
        }
        assert(nflats_ == 2);
        SzlOps* key_ops = NULL;
        SzlType::Kind key_kind = flats_[0];
        if (flat_ops_ != NULL && flat_ops_[0] != NULL) {
          key_ops = flat_ops_[0];
        }
        SzlOps* value_ops = NULL;
        SzlType::Kind value_kind = flats_[1];
        if (flat_ops_ != NULL && flat_ops_[1] != NULL) {
          value_ops = flat_ops_[1];
        }
        // First pass: compare keys.
        vector<int> s_target_i;
        vector<int> d_target_i;
        vector<bool> s_target_copy;
        s_target_i.reserve(s.s.len);
        d_target_i.reserve(d->s.len);
        s_target_copy.reserve(s.s.len);

        int s_i = 0;
        int d_i = 0;
        int target_i = 0;

        while (s_i < s.s.len || d_i < d->s.len) {
          if (s_i < s.s.len && d_i < d->s.len) {
            int cmp_res = 0;

            if (key_ops != NULL) {
              cmp_res = key_ops->Cmp(s.s.vals + s_i, d->s.vals + d_i);
            } else {
              cmp_res = CmpBase(
                  key_kind, s.s.vals + s_i, d->s.vals + d_i);
            }
            if (cmp_res < 0 || cmp_res == 0) {
              s_target_i.push_back(target_i);
              s_target_copy.push_back(cmp_res != 0);
              s_i += nflats_;
            }
            if (cmp_res > 0 || cmp_res == 0) {
              d_target_i.push_back(target_i);
              d_i += nflats_;
            }
          } else if (s_i < s.s.len) {
            assert(d_i >= d->s.len);
            s_target_i.push_back(target_i);
            s_target_copy.push_back(true);
            s_i += nflats_;
          } else {
            assert(d_i < d->s.len);
            d_target_i.push_back(target_i);
            d_i += nflats_;
          }
          ++target_i;
        }
        assert(s_target_i.size() * nflats_ == s.s.len);
        assert(d_target_i.size() * nflats_ == d->s.len);
        assert(s_target_i.size() == s_target_copy.size());

        // Now know the number of target elements: target_i.
        // Second pass: Copy destination elements (if needed).
        int new_d_len = target_i * nflats_;

        if (new_d_len > d->s.len) {
          SzlValue* new_vals = new SzlValue[new_d_len];

          // Put the values in the right locations.
          for (int i = 0; i < d_target_i.size(); ++i) {
            // We take ownership of all pointers and such. Effectively, we're
            // performing a swap here.
            memcpy(new_vals + d_target_i[i] * nflats_,
                   d->s.vals + i * nflats_,
                   sizeof(SzlValue) * nflats_);
          }
          delete [] d->s.vals;
          d->s.vals = new_vals;
          d->s.len = new_d_len;
        }
        // Add source to destination.
        for (int i = 0; i < s_target_i.size(); ++i) {
          // If this is a new key, just copy the results.
          if (s_target_copy[i]) {
            for (int z = 0; z < nflats_; ++z) {
              int s_j = i * nflats_ + z;
              int d_j = s_target_i[i] * nflats_ + z;
              assert(d_j < d->s.len);
              int flat_j = d_j % nflats_;
              SzlType::Kind k = flats_[flat_j];

              if (flat_ops_ != NULL && flat_ops_[flat_j] != NULL) {
                flat_ops_[flat_j]->Assign(s.s.vals[s_j], &(d->s.vals[d_j]));
              } else if (k == SzlType::STRING || k == SzlType::BYTES) {
                ReplaceSzlValueBuf(s.s.vals[s_j].s.buf, s.s.vals[s_j].s.len,
                                   &d->s.vals[d_j]);
              } else if (k == SzlType::FLOAT) {
                d->s.vals[d_j].f = s.s.vals[s_j].f;
              } else if (IsSimpleBaseKind(k)) {
                d->s.vals[d_j].i = s.s.vals[s_j].i;
              } else {
                LOG(FATAL) << "can't add for " << type_;
                return;
              }
            }
          } else {
            int s_j = i * nflats_ + (nflats_ - 1);
            int d_j = s_target_i[i] * nflats_ + (nflats_ - 1);

            // Sum source to destination.
            if (value_ops) {
              value_ops->Add(s.s.vals[s_j], &(d->s.vals[d_j]));
            } else if (value_kind == SzlType::FLOAT) {
              d->s.vals[d_j].f += s.s.vals[s_j].f;
            } else if (IsSimpleBaseKind(value_kind)) {
              d->s.vals[d_j].i += s.s.vals[s_j].i;
            } else {
              LOG(FATAL) << "can't add for " << type_;
              return;
            }
          }
        }
      }
      break;
    default:
      LOG(FATAL) << "can't add for " << type_;
      return;
  }
}

// d -= s
void SzlOps::Sub(const SzlValue& s, SzlValue* d) const {
  switch (type_.kind()) {
    case SzlType::BOOL:
    case SzlType::FINGERPRINT:
    case SzlType::INT:
    case SzlType::TIME:
      d->i -= s.i;
      break;
    case SzlType::FLOAT:
      d->f -= s.f;
      break;
    case SzlType::TUPLE:
      if (s.s.len) {
        assert(s.s.len == nflats_);
        if (!d->s.len) {
          Negate(s, d);
          return;
        }
        for (int i = 0; i < nflats_; ++i) {
          SzlType::Kind k = flats_[i];
          if (flat_ops_ != NULL && flat_ops_[i] != NULL) {
            flat_ops_[i]->Sub(s.s.vals[i], &(d->s.vals[i]));
          } else if (k == SzlType::FLOAT) {
            d->s.vals[i].f -= s.s.vals[i].f;
          } else {
            d->s.vals[i].i -= s.s.vals[i].i;
          }
        }
      }
      break;
    default:
      LOG(FATAL) << "can't sub for " << type_;
      return;
  }
}

void SzlOps::AppendToString(const SzlValue& v, string* output) const {
  SzlEncoder enc;
  Encode(v, &enc);
  enc.Swap(output);
}

bool SzlOps::ParseFromArray(const char* buf, int len, SzlValue* val) const {
  SzlDecoder dec(buf, len);
  return Decode(&dec, val);
}

static inline void SzlOpsDoEncode(SzlType::Kind kind,
                                  const SzlValue& s, SzlEncoder* enc) {
  switch (kind) {
    case SzlType::BOOL:
      enc->PutBool(s.i);
      break;
    case SzlType::FINGERPRINT:
      enc->PutFingerprint(s.i);
      break;
    case SzlType::INT:
      enc->PutInt(s.i);
      break;
    case SzlType::TIME:
      enc->PutTime(s.i);
      break;
    case SzlType::FLOAT:
      enc->PutFloat(s.f);
      break;
    case SzlType::STRING: {
        const char* buf = s.s.buf;
        int len;
        if (buf == NULL) {
          buf = "";
          len = 0;
        } else {
          len = s.s.len - 1;            // exclude terminating zero
        }
        enc->PutString(buf, len);
      }
      break;
    case SzlType::BYTES:
      enc->PutBytes(s.s.buf, s.s.len);
      break;
    default:
      LOG(FATAL) << "can't encode for kind " << kind;
      break;
  }
}

// Encode a value to enc.
static void EncodeDefaultBase(SzlType::Kind kind, SzlEncoder* enc) {
  switch (kind) {
    case SzlType::BOOL:
      enc->PutBool(false);
      break;
    case SzlType::FINGERPRINT:
      enc->PutFingerprint(0);
      break;
    case SzlType::INT:
      enc->PutInt(0);
      break;
    case SzlType::TIME:
      enc->PutTime(0);
      break;
    case SzlType::FLOAT:
      enc->PutFloat(0);
      break;
    case SzlType::STRING:
      enc->PutString("", 0);
      break;
    case SzlType::BYTES:
      enc->PutBytes("", 0);
      break;
    default:
      LOG(FATAL) << "can't encode for " << kind;
      break;
  }
}

// Encode a value to enc.
void SzlOps::EncodeDefault(SzlEncoder* enc) const {
  if (type_.kind() == SzlType::TUPLE) {
    for (int i = 0; i < nflats_; ++i) {
      int flat_i = i % nflats_;
      if (flat_ops_ != NULL && flat_ops_[flat_i] != NULL) {
        flat_ops_[flat_i]->EncodeDefault(enc);
      } else {
        EncodeDefaultBase(flats_[flat_i], enc);
      }
    }
  } else if (type_.kind() != SzlType::MAP) {
    EncodeDefaultBase(type_.kind(), enc);
  }
}

// Encode a value to enc.
void SzlOps::EncodeInternal(const SzlValue& s,
                            SzlEncoder* enc,
                            bool top_level) const {
  if (type_.kind() == SzlType::TUPLE) {
    if (!top_level) {
      enc->Start(type_.kind());
    }
    if (s.s.len) {
      assert(s.s.len == nflats_);
      for (int i = 0; i < s.s.len; ++i) {
        if (flat_ops_ != NULL && flat_ops_[i] != NULL) {
          flat_ops_[i]->EncodeInternal(s.s.vals[i], enc, false);
        } else {
          SzlOpsDoEncode(flats_[i], s.s.vals[i], enc);
        }
      }
    } else {
      EncodeDefault(enc);
    }
    if (!top_level) {
      enc->End(type_.kind());
    }
  } else if (type_.kind() == SzlType::MAP || type_.kind() == SzlType::ARRAY) {
    enc->Start(type_.kind());

    // A map is variable length and hence we need to explicitly encode the
    // length. Actually, we don't have to, but it's more efficient for
    // decoding. This is true for arrays too, but originally arrays were not
    // supported here and the code that emits arrays (in sawzall) doesn't emit
    // the length. We should consider fixing this at some point.
    if (type_.kind() == SzlType::MAP) {
      enc->PutInt(s.s.len);
    }
    if (s.s.len) {
      for (int i = 0; i < s.s.len; ++i) {
        int flat_i = i % nflats_;
        if (flat_ops_ != NULL && flat_ops_[flat_i] != NULL) {
          flat_ops_[flat_i]->EncodeInternal(s.s.vals[i], enc, false);
        } else {
          SzlOpsDoEncode(flats_[flat_i], s.s.vals[i], enc);
        }
      }
    }
    enc->End(type_.kind());
  } else {
    SzlOpsDoEncode(type_.kind(), s, enc);
  }
}

// Encode a value to enc.
void SzlOps::Encode(const SzlValue& s, SzlEncoder* enc) const {
  EncodeInternal(s, enc, true);
}

// Decode a value from dec.
static inline bool SzlOpsDoDecode(SzlType::Kind kind,
                                  SzlDecoder* dec, SzlValue* val) {
  switch (kind) {
    case SzlType::BOOL: {
        bool v;
        if (!dec->GetBool(&v))
          return false;
        val->i = v;
      }
      break;
    case SzlType::FINGERPRINT: {
        uint64 v;
        if (!dec->GetFingerprint(&v))
          return false;
        val->i = v;
      }
      break;
    case SzlType::INT:
      return dec->GetInt(&val->i);
    case SzlType::TIME: {
        uint64 v;
        if (!dec->GetTime(&v))
          return false;
        val->i = v;
      }
      break;
    case SzlType::FLOAT:
      return dec->GetFloat(&val->f);
    case SzlType::STRING: {
        string str;
        if (!dec->GetString(&str))
          return false;
        // Internally, saw strings must be null terminated,
        // and empty strings must have length 0.
        if (str.size() == 0) {
          delete[] val->s.buf;
          val->s.buf = NULL;
          val->s.len = 0;
        } else {
          ReplaceSzlValueBuf(str.c_str(), str.size() + 1, val);
        }
      }
      break;
    case SzlType::BYTES: {
        string str;
        if (!dec->GetBytes(&str))
          return false;
        ReplaceSzlValueBuf(str.data(), str.size(), val);
      }
      break;
    default:
      LOG(ERROR) << "cannot decode for kind " << kind << " (\""
                 << SzlType::KindName(kind) << "\") -- not supported";
      return false;
  }
  return true;
}

// Decode a value from dec.
bool SzlOps::DecodeInternal(SzlDecoder* dec,
                            SzlValue* val,
                            bool top_level) const {
  if (type_.kind() == SzlType::TUPLE) {
    if (!top_level && !dec->GetStart(type_.kind())) {
      LOG(ERROR) << "Unable to get tuple start";
      return false;
    }
    if (!val->s.len) {
      CHECK(val->s.vals == NULL);
      val->s.vals = new SzlValue[nflats_];
      val->s.len = nflats_;
    }
    assert(val->s.len == nflats_);

    for (int i = 0; i < nflats_; ++i) {
      if (flat_ops_ != NULL && flat_ops_[i] != NULL) {
        if (!flat_ops_[i]->DecodeInternal(dec, &val->s.vals[i], false)) {
          return false;
        }
      } else {
        if (!SzlOpsDoDecode(flats_[i], dec, &val->s.vals[i])) {
          return false;
        }
      }
    }
    if (!top_level && !dec->GetEnd(type_.kind())) {
      LOG(ERROR) << "Unable to get tuple end";
      return false;
    }
  } else if (type_.kind() == SzlType::MAP || type_.kind() == SzlType::ARRAY) {
    int64 len = 0;

    if (!dec->GetStart(type_.kind())) {
      LOG(ERROR) << "Unable to get the map/array start";
      return false;
    }
    // Get the length of the map or array. Arrays don't emit their size and
    // hence we need to first count the number of elements. This is obviously
    // not very efficient and we should consider changing the way arrays are
    // emitted.
    if (type_.kind() == SzlType::MAP) {
      if (!dec->GetInt(&len)) {
        LOG(ERROR) << "Unable to get the length of the map";
        return false;
      }
    } else {
      // Create a temporary decoder that we'll use to count the number
      // of elements in the array.
      CHECK_EQ(nflats_, 1);  // Array always have a single value type.
      SzlDecoder dec2(dec->position(),
                      static_cast<int>(dec->end() - dec->position()));

      while (!dec2.done()) {
        // End of array?
        if (dec2.IsEnd(type_.kind())) {
          break;
        }
        // Got an element. Skip it.
        if (flat_ops_ != NULL && flat_ops_[0] != NULL) {
          if (!flat_ops_[0]->SkipInternal(&dec2, false)) {
            LOG(ERROR) << "Unable to count length of array";
            return false;
          }
        } else if (!dec2.Skip(flats_[0])) {
          LOG(ERROR) << "Unable to count length of array";
          return false;
        }
        ++len;
      }
    }
    if (len != val->s.len) {
      Clear(val);
    }
    if (len > 0 && len != val->s.len) {
      CHECK(val->s.vals == NULL);
      val->s.vals = new SzlValue[len];
      val->s.len = len;
    }
    for (int i = 0; i < val->s.len; ++i) {
      int flat_i = i % nflats_;
      if (flat_ops_ != NULL && flat_ops_[flat_i] != NULL) {
        if (!flat_ops_[flat_i]->DecodeInternal(dec, &val->s.vals[i], false)) {
          return false;
        }
      } else if (!SzlOpsDoDecode(flats_[flat_i], dec, &val->s.vals[i])) {
        return false;
      }
    }
    if (!dec->GetEnd(type_.kind())) {
      LOG(ERROR) << "Unable to get map/array end";
      return false;
    }
  } else {
    return SzlOpsDoDecode(type_.kind(), dec, val);
  }
  return true;
}

// Decode a value from dec.
bool SzlOps::Decode(SzlDecoder* dec, SzlValue* val) const {
  return DecodeInternal(dec, val, true);
}

// Skip the value in dec.  Returns whether the value had the correct form.
bool SzlOps::SkipInternal(SzlDecoder* dec, bool top_level) const {
  switch (type_.kind()) {
    case SzlType::BOOL:
    case SzlType::BYTES:
    case SzlType::FINGERPRINT:
    case SzlType::INT:
    case SzlType::FLOAT:
    case SzlType::STRING:
    case SzlType::TIME:
      return dec->Skip(type_.kind());

    case SzlType::TUPLE:
      if (!top_level && !dec->GetStart(type_.kind())) {
        return false;
      }
      for (int i = 0; i < nflats_; ++i) {
        if (flat_ops_ != NULL && flat_ops_[i] != NULL) {
          if (!flat_ops_[i]->SkipInternal(dec, false)) {
            return false;
          }
        } else if (!dec->Skip(flats_[i])) {
          return false;
        }
      }
      if (!top_level && !dec->GetEnd(type_.kind())) {
        return false;
      }
      break;

    case SzlType::MAP:
      if (!dec->GetStart(type_.kind())) {
        return false;
      }
      // Must get the length, can't skip that.
      {
        int64 len = 0;
        if (!dec->GetInt(&len)) {
          LOG(ERROR) << "Unable to get the length of the map";
          return false;
        }
        for (int i = 0; i < len; ++i) {
          int flat_i = i % nflats_;
          if (flat_ops_ != NULL && flat_ops_[flat_i] != NULL) {
            if (!flat_ops_[flat_i]->SkipInternal(dec, false)) {
              return false;
            }
          } else if (!dec->Skip(flats_[flat_i])) {
            return false;
          }
        }
      }
      if (!dec->GetEnd(type_.kind())) {
        return false;
      }
      break;

    case SzlType::ARRAY:
      if (!dec->GetStart(type_.kind())) {
        return false;
      }
      for (int i = 0; !dec->done(); ++i) {
        if (dec->IsEnd(type_.kind())) {
          break;
        }
        int flat_i = i % nflats_;
        if (flat_ops_ != NULL && flat_ops_[flat_i] != NULL) {
          if (!flat_ops_[flat_i]->SkipInternal(dec, false)) {
            return false;
          }
        } else if (!dec->Skip(flats_[flat_i])) {
          return false;
        }
      }
      if (!dec->GetEnd(type_.kind())) {
        return false;
      }
      break;

    // Can't handle other types.
    default:
      return false;
  }
  return true;
}

// Skip the value in dec.  Returns whether the value had the correct form.
bool SzlOps::Skip(SzlDecoder* dec) const {
  return SkipInternal(dec, true);
}
