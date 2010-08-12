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

#include <string>

#include "public/porting.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "utilities/strutils.h"

#include "public/szltype.h"
#include "public/szlencoder.h"
#include "emitvalues/szlencoding.h"


static inline void packUint32(unsigned char *p, uint32 x)  {
  p[0] = x >> 24;
  p[1] = x >> 16;
  p[2] = x >> 8;
  p[3] = x;
}

static inline void packUint64(unsigned char *p, uint64 x)  {
  uint32 h = static_cast<uint32>(x >> 32);
  uint32 l = static_cast<uint32>(x);
  packUint32(p, h);
  packUint32(p+4, l);
}

// Pack a positive number with a variable-length tag encode.
// Tags are assigned from tagp1 to tagp1 + 7, indiciting the
// number of bytes following the tag.
void SzlEncoder::PackP8(int tagp1, uint64 v) {
  // Find the number of bytes needed to hold a non-negative number.
  // start === 8 - bytes_to_hold(v);
  unsigned char buf[9];
  int start = 0;
  for (int i = 1; i < 7; i++) {
    if (v < (1LL << (i << 3))) {
      start = 8 - i;
      break;
    }
  }

  // Pack all 8 bytes in wire format, leaving a byte a the front for a tag.
  packUint64(&buf[1], v);

  // We know the wire is big-endian, so we end up just
  // needing the bytes from (start + 1) .. 9.
  // Slap the tag in at position start, and write the value.
  buf[start] = tagp1 + 7 - start;
  AppendEncoding(&buf[start], 9 - start);
}

// Pack a negative number with a variable-length tag encode.
// Tags are assigned from tagn1 - 7 to tagn1, indiciting the
// number of bytes following the tag.
void SzlEncoder::PackN8(int tagp1, int64 v) {
  // Negative numbers
  uint64 u = -(v + 1);

  // Find the number of bytes needed to hold the magnitude of the number.
  // start === 8 - bytes_to_hold(v);
  unsigned char buf[9];
  int start = 0;
  for (int i = 1; i < 7; i++) {
    if (u < (1LL << (i << 3))) {
      start = 8 - i;
      break;
    }
  }

  // Pack all 8 bytes in wire format, leaving a byte a the front for a tag.
  packUint64(&buf[1], v);

  // We know the wire is big-endian, so we end up just
  // needing the bytes from (start + 1) .. 9.
  // Slap the tag in at position start, and write the value.
  buf[start] = SzlEncoding::INTN1 - (7 - start);
  AppendEncoding(&buf[start], 9 - start);
}

SzlEncoder::SzlEncoder() {
  SetVersion(kSzlFileVersion);
}

bool SzlEncoder::SetVersion(const string& version) {
  // Only know about version 1.0
  if (version == kSzlFileVersion) {
    version_ = 10;
    return true;
  }

  // Add more versions here if needed.

  return false;
}

void SzlEncoder::Reset() {
  data_.clear();
}

void SzlEncoder::Swap(string *s) {
  data_.swap(*s);
  data_.clear();
}

void SzlEncoder::Start(SzlType::Kind kind) {
  int k = 0;
  if (kind == SzlType::ARRAY) {
    k = SzlEncoding::ARRAY_START;
  } else if (kind == SzlType::MAP) {
    k = SzlEncoding::MAP_START;
  } else if (kind == SzlType::TUPLE) {
    k = SzlEncoding::TUPLE_START;
  } else {
    LOG(FATAL) << "bad kind " << kind << " in SzlEncoder::Start";
  }
  data_.push_back(k);
}

void SzlEncoder::End(SzlType::Kind kind) {
  int k = 0;
  if (kind == SzlType::ARRAY) {
    k = SzlEncoding::ARRAY_END;
  } else if (kind == SzlType::MAP) {
    k = SzlEncoding::MAP_END;
  } else if (kind == SzlType::TUPLE) {
    k = SzlEncoding::TUPLE_END;
  } else {
    LOG(FATAL) << "bad kind " << kind << " in SzlEncoder::End";
  }
  data_.push_back(k);
}

void SzlEncoder::PutBool(bool b) {
  data_.push_back(SzlEncoding::BOOL_FALSE + b);
}

void SzlEncoder::PutBytes(const unsigned char *p, int len) {
  data_.push_back(SzlEncoding::BYTES);

  const unsigned char* segment = p;
  const unsigned char* end = p + len;
  for (const unsigned char* q = segment; q < end; q++) {
    // Check for terminator and escape it.
    if (*q == SzlEncoding::kBytesTerm) {
      AppendEncoding(segment, q - segment);
      data_.push_back(SzlEncoding::kBytesTerm);
      segment = q;
    }
  }
  AppendEncoding(segment, end - segment);

  // Add terminating sequence
  buf_[0] = SzlEncoding::kBytesTerm;
  buf_[1] = 0;
  AppendEncoding(buf_, 2);
}

void SzlEncoder::PutInt(int64 v) {
  if (v >= 0)
    PackP8(SzlEncoding::INTP1, v);
  else
    PackN8(SzlEncoding::INTN1, v);
}

void SzlEncoder::PutString(const char *s, int len) {
  data_.push_back(SzlEncoding::STRING);
  AppendEncoding(s, len);
  data_.push_back('\0');
}

void SzlEncoder::PutFingerprint(uint64 fp) {
  PackP8(SzlEncoding::FINGERPRINT1, fp);
}

void SzlEncoder::PutTime(uint64 t) {
  PackP8(SzlEncoding::TIME1, t);
}

void SzlEncoder::PutFloat(double x) {
  data_.push_back(SzlEncoding::FLOAT);
  string s;
  KeyFromDouble(x, &s);
  CHECK_EQ(8, s.size()) << ": Bad encoded length returned by KeyFromDouble";
  data_.append(s);
}

// Parse the key and encode it to the desired format. Returns true
// if the type is allowed (string or int), false otherwise.
bool SzlEncoder::EncodeKeyFromString(const SzlType& type, const string& key,
                                     string *encoded_key, string *error) {
  SzlEncoder enc;
  int64 numeric_key;
  switch (type.kind()) {
    case SzlType::INT:
      numeric_key = strto64(key.c_str(), NULL, 0);
      enc.PutInt(numeric_key);
      break;
    case SzlType::STRING:
      enc.PutString(key.c_str());
      break;
    default:
      *error = "Unsupported type for string encoding";
      return false;
  }
  *encoded_key = enc.data();
  if (type.kind() == SzlType::STRING) {
    // Now we want to convert it to a string that will match all strings
    // that start with the key.
    encoded_key->resize(encoded_key->size() - 1);
  }
  return true;
}
