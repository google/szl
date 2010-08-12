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

#include <float.h>                   // for DBL_DIG
#include <algorithm>
#include <string>


#include "public/porting.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "utilities/strutils.h"

#include "public/szltype.h"
#include "public/szldecoder.h"
#include "emitvalues/szlencoding.h"

namespace {

class SzlDecoderTbl {
 public:
  SzlDecoderTbl();

  int size_[SzlEncoding::NKIND];
  SzlType::Kind kind_[SzlEncoding::NKIND];
};

SzlDecoderTbl tbl;

static inline uint32 unpackUint32(const unsigned char *p)  {
  return (static_cast<uint32>(p[0]) << 24) |
         (static_cast<uint32>(p[1]) << 16) |
         (static_cast<uint32>(p[2]) << 8) |
         static_cast<uint32>(p[3]);
}

static inline uint64 unpackUint64(const unsigned char *p)  {
  return (static_cast<uint64>(unpackUint32(p)) << 32) | unpackUint32(p+4);
}

// Unpack a positive integer, whose encoded length is determined by the
// tag used.  Tags are from tagp1 to tagp1 + 7.
static int UnpackP8(int tagp1, const unsigned char* p, int size, uint64* v) {
  if (size < 1) {
    return 0;
  }

  // Check for a tag in the range tagp1 .. tagp8
  int k = *p++;
  if (k < tagp1 || k > tagp1 + 7) {
    return 0;
  }
  k -= tagp1 - 1;
  if (size < k + 1) {
    return 0;
  }

  // Decode the right number of bytes, doing most of the math in 32bit ints,
  // which should be faster than 64 bit ints on many machines.
  uint32 t1 = 0;
  int k4 = MinInt(k, 4);
  for (int i = 0; i < k4; i++) {
    t1 = (t1 << 8) | p[i];
  }
  if (k > 4) {
    uint32 t2 = 0;
    p += 4;
    for (int i = 0; i < k - 4; i++) {
      t2 = (t2 << 8) | p[i];
    }
    *v = (static_cast<uint64>(t1) << ((k - 4) << 3)) | t2;
  } else {
    *v = t1;
  }

  return k + 1;
}

// Unpack a negative integer, whose encoded length is determined by the
// tag used.  Tags are from tagn1 - 7 to tagn1
static int UnpackN8(int tagn1, const unsigned char* p, int size, int64* v) {
  if (size < 1) {
    return 0;
  }

  // Check for a tag in the range tagn8 .. tagn1
  int k = *p++;
  if (k > tagn1 || k < tagn1 - 7) {
    return 0;
  }
  k = (tagn1 - k) + 1;
  if (size < k + 1) {
    return 0;
  }

  // Decode the right number of bytes, doing most of the math in 32bit ints,
  // which should be faster than 64 bit ints on many machines.
  uint32 t1 = ~0;
  int k4 = MinInt(k, 4);
  for (int i = 0; i < k4; i++) {
    t1 = (t1 << 8) | p[i];
  }
  if (k > 4) {
    uint32 t2 = 0;
    p += 4;
    for (int i = 0; i < k - 4; i++) {
      t2 = (t2 << 8) | p[i];
    }
    uint64 uv = t1;
    uv = (uv << ((k - 4) << 3)) | t2;
    if (k != 8) {
      uv |= ~0ULL << (k << 3);
    }
    *v = uv;
  } else {
    *v = (~0LL << 32) | t1;
  }


  return k + 1;
}

}  // namespace

SzlDecoderTbl::SzlDecoderTbl() {
  size_[SzlEncoding::VOID] = 0;
  kind_[SzlEncoding::VOID] = SzlType::VOID;

  size_[SzlEncoding::FLOAT] = 9;
  kind_[SzlEncoding::FLOAT] = SzlType::FLOAT;
  size_[SzlEncoding::STRING] = 1;
  kind_[SzlEncoding::STRING] = SzlType::STRING;

  size_[SzlEncoding::BOOL_FALSE] = 1;
  kind_[SzlEncoding::BOOL_FALSE] = SzlType::BOOL;
  size_[SzlEncoding::BOOL_TRUE] = 1;
  kind_[SzlEncoding::BOOL_TRUE] = SzlType::BOOL;

  size_[SzlEncoding::BYTES] = 1;
  kind_[SzlEncoding::BYTES] = SzlType::BYTES;

  size_[SzlEncoding::ARRAY_START] = 1;
  kind_[SzlEncoding::ARRAY_START] = SzlType::ARRAY;
  size_[SzlEncoding::ARRAY_END] = 1;
  kind_[SzlEncoding::ARRAY_END] = SzlType::ARRAY;

  size_[SzlEncoding::TUPLE_START] = 1;
  kind_[SzlEncoding::TUPLE_START] = SzlType::TUPLE;
  size_[SzlEncoding::TUPLE_END] = 1;
  kind_[SzlEncoding::TUPLE_END] = SzlType::TUPLE;

  size_[SzlEncoding::MAP_START] = 1;
  kind_[SzlEncoding::MAP_START] = SzlType::MAP;
  size_[SzlEncoding::MAP_END] = 1;
  kind_[SzlEncoding::MAP_END] = SzlType::MAP;

  for (int i = 0; i < 8; i++) {
    size_[SzlEncoding::FINGERPRINT1 + i] = 2 + i;
    kind_[SzlEncoding::FINGERPRINT1 + i] = SzlType::FINGERPRINT;

    size_[SzlEncoding::INTP1 + i] = 2 + i;
    kind_[SzlEncoding::INTP1 + i] = SzlType::INT;

    size_[SzlEncoding::INTN1 - i] = 2 + i;
    kind_[SzlEncoding::INTN1 - i] = SzlType::INT;

    size_[SzlEncoding::TIME1 + i] = 2 + i;
    kind_[SzlEncoding::TIME1 + i] = SzlType::TIME;
  }
}

SzlDecoder::SzlDecoder()
  : start_(NULL),
    end_(NULL),
    p_(NULL) {
}

SzlDecoder::SzlDecoder(unsigned const char *p, int len)
  : start_(p),
    end_(start_ + len),
    p_(start_) {
}

SzlDecoder::SzlDecoder(const char *p, int len)
  : start_(reinterpret_cast<const unsigned char *>(p)),
    end_(start_ + len),
    p_(start_) {
}

void SzlDecoder::Init(const unsigned char *p, int len) {
  start_ = p_ = p;
  end_ = p + len;
}

void SzlDecoder::Restart() {
  p_ = start_;
}

SzlType::Kind SzlDecoder::peek() const {
  if (done())
    return SzlType::VOID;

  int k = *p_;
  if (k < SzlEncoding::NKIND) {
    return tbl.kind_[k];
  }
  return SzlType::VOID;
}

bool SzlDecoder::IsStart(SzlType::Kind kind) {
  int k = 0;
  if (kind == SzlType::ARRAY) {
    k = SzlEncoding::ARRAY_START;
  } else if (kind == SzlType::MAP) {
    k = SzlEncoding::MAP_START;
  } else if (kind == SzlType::TUPLE) {
    k = SzlEncoding::TUPLE_START;
  } else {
    return false;
  }
  if (size() < 1 || *p_ != k)
    return false;
  return true;
}

bool SzlDecoder::GetStart(SzlType::Kind kind) {
  if (!IsStart(kind))
    return false;
  p_ += 1;
  return true;
}

bool SzlDecoder::IsEnd(SzlType::Kind kind) {
  int k = 0;
  if (kind == SzlType::ARRAY) {
    k = SzlEncoding::ARRAY_END;
  } else if (kind == SzlType::MAP) {
    k = SzlEncoding::MAP_END;
  } else if (kind == SzlType::TUPLE) {
    k = SzlEncoding::TUPLE_END;
  } else {
    return false;
  }
  if (size() < 1 || *p_ != k)
    return false;
  return true;
}

bool SzlDecoder::GetEnd(SzlType::Kind kind) {
  if (!IsEnd(kind))
    return false;
  p_ += 1;
  return true;
}

bool SzlDecoder::GetBool(bool *v) {
  // New encoding
  if (size() < 1) {
    return false;
  }
  int k = *p_;
  if (k == SzlEncoding::BOOL_FALSE || k == SzlEncoding::BOOL_TRUE) {
    *v = k - SzlEncoding::BOOL_FALSE;
    p_ += 1;
    return true;
  }

  // Old encoding
  if (size() < 2 || peek() != SzlType::BOOL)
    return false;
  *v = static_cast<bool>(p_[1]);
  p_ += 2;
  return true;
}

bool SzlDecoder::GetBytes(string *v) {
  // New encoding
  if (size() < 1) {
    return false;
  }
  int k = *p_;
  if (k == SzlEncoding::BYTES) {
    v->clear();
    const unsigned char *q;
    const unsigned char* segment = p_ + 1;
    for (q = segment; ; q++) {
      if (q + 1 >= end_) {
        return false;
      }

      // Check for terminator, or escaped terminator.
      if (*q == SzlEncoding::kBytesTerm) {
        v->append(reinterpret_cast<const char*>(segment), q - segment);
        if (q[1] == 0) {
          p_ = q + 2;
          return true;
        }
        if (q[1] != SzlEncoding::kBytesTerm) {
          return false;
        }

        // Next time we will copy the terminator character.
        segment = q + 1;
        q++;
      }
    }
  }

  // Old encoding
  if (size() < 5 || peek() != SzlType::BYTES)
    return false;
  int n = unpackUint32(p_ + 1);
  if (size() < n + 5)
    return false;
  v->clear();
  v->append(reinterpret_cast<const char*>(p_) + 5, n);
  p_ += 5 + n;
  return true;
}

bool SzlDecoder::GetInt(int64 *v) {
  // New encoding, positive ints
  uint64 uv;
  int n = UnpackP8(SzlEncoding::INTP1, p_, size(), &uv);
  if (n > 0) {
    *v = uv;
    p_ += n;
    return true;
  }

  // Negative ints
  n = UnpackN8(SzlEncoding::INTN1, p_, size(), v);
  if (n > 0) {
    p_ += n;
    return true;
  }

  // Old encoding
  if (size() < 9 || peek() != SzlType::INT)
    return false;
  uint64 x = unpackUint64(p_ + 1);
  x -= (1ULL << 63);
  *v = x;
  p_ += 9;
  return true;
}

bool SzlDecoder::GetFingerprint(uint64 *v) {
  // New encoding
  int n = UnpackP8(SzlEncoding::FINGERPRINT1, p_, size(), v);
  if (n > 0) {
    p_ += n;
    return true;
  }

  // Old encoding
  if (size() < 9 || peek() != SzlType::FINGERPRINT)
    return false;
  *v = unpackUint64(p_ + 1);
  p_ += 9;
  return true;
}

bool SzlDecoder::GetTime(uint64 *v) {
  // New encoding
  int n = UnpackP8(SzlEncoding::TIME1, p_, size(), v);
  if (n > 0) {
    p_ += n;
    return true;
  }

  // Old encoding
  if (size() < 9 || peek() != SzlType::TIME)
    return false;
  *v = unpackUint64(p_ + 1);
  p_ += 9;
  return true;
}

bool SzlDecoder::GetString(string *v) {
  if (size() < 1 || peek() != SzlType::STRING)
    return false;

  const unsigned char *q;
  for (q = p_ + 1; q < end_ && *q; q++)
    ;
  if (q >= end_)
    return false;
  v->clear();
  v->append(reinterpret_cast<const char*>(p_ + 1), q - (p_ + 1));
  p_ = q + 1;
  return true;
}

bool SzlDecoder::GetFloat(double *v) {
  // New encoding
  if (size() < 1) {
    return false;
  }
  int k = *p_;
  if (k == SzlEncoding::FLOAT) {
    if (size() < 9) {
      return false;
    }

    *v = KeyToDouble(string(reinterpret_cast<const char*>(&p_[1]), 8));
    p_ += 9;
    return true;
  }

  // Old encoding
  if (size() < 10 || peek() != SzlType::FLOAT)
    return false;
  uint64 i = unpackUint64(p_ + 2);
  double x = *reinterpret_cast<double*>(&i);
  if (p_[1] == 0)
    x = -x;
  *v = x;
  p_ += 10;
  return true;
}

bool SzlDecoder::Skip(SzlType::Kind kind) {
  if (done() || kind != peek()) {
    return false;
  }
  int k = *p_;
  if (k >= SzlEncoding::NKIND || tbl.size_[k] == 0 || size() < tbl.size_[k])
    return false;

  switch (k) {
  default:
    break;
  case SzlEncoding::BYTES: {
      for (const unsigned char* q = p_ + 1; ; q++) {
        if (q + 1 >= end_)
          return false;
        if (*q == SzlEncoding::kBytesTerm) {
          q++;
          if (*q == 0) {
            p_ = q + 1;
            return true;
          }
          if (*q != SzlEncoding::kBytesTerm) {
            return false;
          }
        }
      }
    }
    break;
  case SzlEncoding::STRING: {
      const unsigned char *q;
      for (q = p_ + 1; q < end_ && *q; q++)
        ;
      if (q >= end_)
        return false;
      p_ = q;
    }
    break;
  }

  p_ += tbl.size_[k];
  return true;
}

bool SzlDecoder::Advance(int num_values) {
  int i = 0;
  for (;;) {
    SzlType::Kind kind = peek();

    // Skip all end markers.
    while (IsEnd(kind)) {
      Skip(kind);
      kind = peek();
    }

    // See whether we are done.
    if (kind == SzlType::VOID)
      return false;
    if (i >= num_values)
      return true;

    // Skip all start markers.
    while (IsStart(kind)) {
      Skip(kind);
      kind = peek();
    }

    // Advanve one primitive value.
    Skip(kind);
    i++;
  }
}

// "Pretty"-print the SzlDecoder value. This function outputs
// a single "logical unit". E.g., if the next type is an
// array, the whole array is output.
bool SzlDecoder::PPrintSingleUnit(string* result) {
  DCHECK(result != NULL);
  SzlType::Kind kind = peek();

  switch (kind) {
    default:
      return false;
    case SzlType::ARRAY:
    case SzlType::TUPLE:
      if (!GetStart(kind))
        return false;
      (*result) += "{ ";
      {
        bool comma = false;

        while (!done()) {
          if (IsEnd(kind)) {
            break;
          }
          if (comma)
            (*result) += ", ";
          else
            comma = true;

          // Not the end. Print a value.
          if (!PPrintSingleUnit(result))
            return false;
        }
      }
      if (!GetEnd(kind)) {
        LOG(ERROR) << "Did not find end of array or tuple.";
        return false;
      }
      (*result) += " }";
      break;

    // Need special code for maps, since there are key:value pairs
    // (unlike array and tuples). Hence, use recursion.
    case SzlType::MAP: {
      if (!GetStart(kind))
        return false;

      // Get the length of the array.
      int64 len = 0;
      if (!GetInt(&len) || len < 0) {
        LOG(ERROR) << "Unable to get the length of the map";
        return false;
      }
      // If this is an empty map, deal with that separately.
      if (len == 0) {
        if (!GetEnd(kind)) {
          LOG(ERROR) << "Did not find end of map.";
          return false;
        }
        (*result) += "{:}";
        break;
      }
      (*result) += "{ ";
      {
        bool comma = false;
        int64 items_left = len;

        while (items_left > 0) {
          if (IsEnd(kind)) {
            LOG(ERROR) << "Found premature end of map. Expected "
                       << items_left << " more elements.";
            return false;
          }
          if (comma)
            (*result) += ", ";
          else
            comma = true;

          // Not the end. Print key: value.
          if (!PPrintSingleUnit(result))
            return false;
          (*result) += ": ";
          if (!PPrintSingleUnit(result))
            return false;
          items_left -= 2;  // One key and one value.
        }
      }
      if (!GetEnd(kind)) {
        LOG(ERROR) << "Did not find end of map.";
        return false;
      }
      (*result) += " }";
      break;
    }
    case SzlType::TIME: {
        uint64 x;
        if (!GetTime(&x))
          return false;
        StringAppendF(result, "%llu", x);
      }
      break;
    case SzlType::FINGERPRINT: {
        uint64 x;
        if (!GetFingerprint(&x))
          return false;
        StringAppendF(result, "%llu", x);
      }
      break;
    case SzlType::FLOAT: {
        double x;
        if (!GetFloat(&x))
          return false;
        StringAppendF(result, "%.*g", DBL_DIG, x);
      }
      break;
    case SzlType::INT: {
        int64 x;
        if (!GetInt(&x))
          return false;
        StringAppendF(result, "%lld", x);
      }
      break;
    case SzlType::BYTES: {
        string x;
        if (!GetBytes(&x))
          return false;
        (*result) += x;
      }
      break;
    case SzlType::STRING: {
        string x;
        if (!GetString(&x))
          return false;
        (*result) += x;
      }
      break;
    case SzlType::BOOL: {
        bool x;
        if (!GetBool(&x))
          return false;
        if (x)
          (*result) += "true";
        else
          (*result) += "false";
      }
      break;
  }
  return true;
}


// "Pretty"-print the SzlDecoder value.
// Just print out all the components in a key or value.
// Iterate using peek - gives the next type, which allows
// the appropriate extraction function to be used to get and print
// Values are printed comma separated.
string SzlDecoder::PPrint() {
  bool comma = false;
  string result;
  while (!done()) {
    // Note: we must print commas even after empty fields!
    if (comma) {
      result += ", ";
    } else {
      comma = true;
    }
    if (!PPrintSingleUnit(&result)) {
      result += "error decoding!";
      break;
    }
  }
  return result;
}
