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

#ifndef _PUBLIC_SZLDECODER_H__
#define _PUBLIC_SZLDECODER_H__

#include <string>

#include "public/szltype.h"


class SzlDecoder {
 public:
  SzlDecoder();
  SzlDecoder(unsigned const char* p, int len);
  SzlDecoder(const char* p, int len);
  void Init(const unsigned char* p, int len);
  void Init(const char* p, int len) {
    Init(reinterpret_cast<const unsigned char*>(p), len);
  }

  void Restart();
  const unsigned char* start() { return start_; }
  const unsigned char* end() { return end_; }
  const unsigned char* position() { return p_; }

  bool done() const { return p_ >= end_; }
  SzlType::Kind peek() const;
  bool Skip(SzlType::Kind kind);

  // Advances position by num_values, depth-first for composite types.
  // Returns false on decode error or done(). True, otherwise.
  bool Advance(int num_values);

  // "Pretty"-print the SzlDecoder value.
  //
  // Prints out all the components in a key or value in human readable,
  // comma-separated format (not proper CSV, as strings aren't quoted).
  string PPrint();

  // "Pretty"-print the current value and advance to the next.
  //
  // The output is a single "logical unit", e.g., if the type of the current
  // value is an array, the whole array is output. The format of each type is:
  //
  //  * SzlType::INT: signed decimal int (i.e. %lld)
  //  * SzlType::FINGERPRINT, SzlType::TIME: unsigned decimal int (i.e. %llu)
  //  * SzlType::BYTES, SzlType::STRING: unquoted string (i.e. %s)
  //  * SzlType::FLOAT: decimal floating point with max precision (i.e. %.*g)
  //  * SzlType::BOOL: "true" or "false"
  //  * SzlType::ARRAY, SzlType::TUPLE: curly-braced, comma separated list (e.g.
  //        {1, 2, 3} or {red, green, blue})
  //  * SzlType::MAP: curly-braced, comma separated list of colon separated
  //        key-value (e.g. {1: one, 2: two, 3: three})
  bool PPrintSingleUnit(string* result);

  bool GetBool(bool* b);
  bool GetBytes(string* s);
  bool GetInt(int64* i);
  bool GetFloat(double* d);
  bool GetFingerprint(uint64* fp);
  bool GetString(string* s);
  bool GetTime(uint64* t);

  // Array and tuple marker checking.
  bool IsStart(SzlType::Kind kind);
  bool IsEnd(SzlType::Kind kind);
  bool GetStart(SzlType::Kind kind);
  bool GetEnd(SzlType::Kind kind);

 private:
  size_t size() { return end_ - p_; }

  const unsigned char* start_;
  const unsigned char* end_;
  const unsigned char* p_;
};

#endif  // _PUBLIC_SZLDECODER_H__
