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

// Encoding for basic values stored in a mill or being processed by szl.
// Typically a sequence of values is encoded without any structural markers.
// However, in the case of arrays the beginning and end of the array are
// demarcated by Start and End called with SzlType::ARRAY.  If the array's
// elements are tuples, each element is demarcated by Start and End called
// with SzlType::TUPLE.
//
// The encoding is designed such that, for scalar values, the sorting order
// of the encoded and unencoded values is the same.
//
// Each encoded value begins with a tag value; see szlencodings.h
// for the possibilities.
//
// The current encoding formats are as follows:
// Bool: length: 1
//   Two tags are used: one for false, one for true.
// Bytes: length: 1 + len of bytes + 2 + escape bytes
//   The bytes are encoded, and those with value kBytesTerm are escaped by
//   inserting another kBytesTerm bytes.  The string is terminated by
//   a two-byte sequence: kBytesTerm, '\0'
// Float: length 1 + 8
//   Encoded using strutil's KeyFromDouble.
// Fingerprint: length: 1 + 1..8
//   8 different tags are used to indicate the number of value bytes,
//   which are in big-endian (most significant byte first) order.
//   Leading zero bytes are dropped.
// Int: length: 1 + 1..8
//   16 different tags are used to indicate sign and the number of value bytes,
//   which are in big-endian order.  Leading sign bytes are dropped.
// String: length: 1 + strlen(s) + 1
//   The contents of the c-style '\0' terminated string.
// Time: length: 1 + 1..8
//   Same encoding as Fingerprint
// Start(ARRAY), End(ARRAY), Start(Tuple), End(Tuple): length: 1
//   Only a tag is encoded for these markers.
//
// Some types had an older encoding.
// Bool: length: 2
//   A value byte of 0 is encoded for false, 1 for true.
// Bytes: length: 1 + 4 + len of bytes
//   A 4 byte length preceeds the data.  Note: this doesn't sort in the
//   same order as the unecoded value.
// Fingerprint, Time: length: 9
//   All 8 value bytes are encoded in big-endian order.
// Int: length: 9
//   All 8 bytes of unsigned value + (1 << 63) are encoded in big-endian order.
// Float: length 10
//   The first byte indicates the sign: 0 for negative, 1 for positive.
//   The next 8 give the 64 bit absolute value.

#ifndef _PUBLIC_SZLENCODER_H__
#define _PUBLIC_SZLENCODER_H__

#include <string>
#include <string.h>

#include "public/szltype.h"


class SzlEncoder {
 public:
  SzlEncoder();

  // Set the version used to encode szl values.
  // By default it encodes using format kSzlFileVersion in szlencodings.h.
  // Returns false if we can't encode using the supplied version.
  bool SetVersion(const string& version);

   // Reset the encoding state, but keep the current version.
  void Reset();

  const string& data() const { return data_; }

  void Swap(string* s);

  // Append an encoded string to the current state.
  void AppendEncoding(const unsigned char*p, int len) {
    data_.append(reinterpret_cast<const char*>(p), len);
  }
  void AppendEncoding(const char*p, int len) {
    data_.append(reinterpret_cast<const char*>(p), len);
  }

  void PutBool(bool b);
  void PutBytes(const unsigned char* p, int len);
  void PutBytes(const char* p, int len) {
    PutBytes(reinterpret_cast<const unsigned char*>(p), len);
  }
  void PutInt(int64 i);
  void PutFloat(double d);
  void PutFingerprint(uint64 fp);
  void PutString(const char* s, int len);
  void PutString(const char* s) { PutString(s, strlen(s)); }
  void PutTime(uint64 t);

  // Mark the start and end of an array, or the start and end of a tuple.
  // Tuple markers are only used to group elements of an array
  void Start(SzlType::Kind kind);
  void End(SzlType::Kind kind);

  // Parse the key and encode it in a format suitable for string prefix
  // delimited scans of mill files. Note that this is distinct from
  // component based key prefix and only works when the first element
  // of the key is a string. Example with 2 component key:
  //   a, 1 = A
  //   a, 2 = B
  //   ab, 1 = C
  //   bc, 1 = D
  // String prefix 'a' returns { (a, 1), (a, 2), (ab, 1) }
  // Component prefix [ 'a' ] returns { (a, 1), (a, 2) }
  // Returns true if the type is allowed (string or int), false otherwise.
  static bool EncodeKeyFromString(const SzlType& type, const string& key,
                                  string *encoded_key, string *error);

 private:
  unsigned char version_;      // version of SzlEncoder format
  unsigned char buf_[18];      // Scratch buffer
  string data_;

  void PackP8(int tagp1, uint64 v);
  void PackN8(int tagp1, int64 v);
};

#endif  // _PUBLIC_SZLENCODER_H__
