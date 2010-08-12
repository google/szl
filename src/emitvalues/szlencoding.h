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

// Tag encodings for SzlEncoder and SzlDecoder

#define kSzlFileVersion "Szl File version 1.0"

class SzlEncoding {
 public:
  enum Tag {
    VOID = 0,        // some values skipped to preserve test results
    STRING = 6,
    BOOL_FALSE = 8,
    BOOL_TRUE,

    BYTES,

    // We have 8 fingerprint tags; they are ordered such that longer
    // fingerprints will sort after shorter ones.
    FINGERPRINT1,       // fingerprint 1 byte needed to encode.
    FINGERPRINT2,
    FINGERPRINT3,
    FINGERPRINT4,
    FINGERPRINT5,
    FINGERPRINT6,
    FINGERPRINT7,
    FINGERPRINT8,       // fingerprint 8 byte needed to encode.

    // int tags, ordered such that encoded and unencoded ints sort identically.
    INTN8,              // negative int, 8 bytes needed to encode.
    INTN7,
    INTN6,
    INTN5,
    INTN4,
    INTN3,
    INTN2,
    INTN1,
    INTP1,              // positive int, 1 byte needed to encode.
    INTP2,
    INTP3,
    INTP4,
    INTP5,
    INTP6,
    INTP7,
    INTP8,              // positive int, 8 bytes needed to encode.

    // 8 time tags, ordered such that longer times will sort after shorter ones.
    TIME1,              // time 1 byte needed to encode.
    TIME2,
    TIME3,
    TIME4,
    TIME5,
    TIME6,
    TIME7,
    TIME8,              // time 8 byte needed to encode.

    ARRAY_START,
    ARRAY_END,

    TUPLE_START,
    TUPLE_END,

    // Floats encoded using KeyFromDouble, so that they have consistent
    // sorting orders.
    FLOAT,

    MAP_START,
    MAP_END,

    NKIND
  };

  // The terminator character for a bytes.  Bytes are terminated by
  // the two bytes kBytesTerm, 0.  If kBytesTerm appears, it is encoded
  // as kBytesTerm, kBytesTerm.
  static const int kBytesTerm = 0x42;
};
