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

namespace sawzall {

// A Factory for frequently used Val*'s.

class Factory {
 public:
  // basic types
  static BoolVal* NewBool(Proc* proc, bool x) {
    assert(bool_t != NULL);  // make sure Factory::Initialize was called
    BoolVal* bv = x ? bool_t : bool_f;
    bv->inc_ref();
    return bv;
  }

  static BytesVal* NewBytes(Proc* proc, int length) {
    return SymbolTable::bytes_form()->NewVal(proc, length);
  }

  static BytesVal* NewBytesC(Proc* proc, const char* x) {
    return SymbolTable::bytes_form()->NewValInit(proc, strlen(x), x);
  }

  static BytesVal* NewBytesInit(Proc* proc, int length, const char* x) {
    return SymbolTable::bytes_form()->NewValInit(proc, length, x);
  }

  static FingerprintVal* NewFingerprint(Proc* proc, szl_fingerprint x) {
    return SymbolTable::fingerprint_form()->NewVal(proc, x);
  }

  static UIntVal* NewUInt(Proc* proc, szl_uint x) {
    return SymbolTable::uint_form()->NewVal(proc, x);
  }

  static FloatVal* NewFloat(Proc* proc, szl_float x) {
    return SymbolTable::float_form()->NewVal(proc, x);
  }

  static IntVal* NewInt(Proc* proc, szl_int x) {
    return SymbolTable::int_form()->NewVal(proc, x);
  }

  static StringVal* NewString(Proc* proc, int length, int num_runes) {
    return SymbolTable::string_form()->NewVal(proc, length, num_runes);
  }

  static StringVal* NewStringC(Proc* proc, szl_string x) {
    return SymbolTable::string_form()->NewValInitCStr(proc, x);
  }


  // These functions are less performance-critical
  static StringVal* NewStringCPP(Proc* proc, const string& x);
  static StringVal* NewStringBytes(Proc* proc, int length, const char* bytes);
  static TimeVal* NewTime(Proc* proc, szl_time x);

  // Arrays
  // The array elements are *not* default initialized.  The caller must
  // initialize every slot, even if an error causes the object to be abandoned.
  static ArrayVal* NewBytesArray(Proc* proc, int length);

  static ArrayVal* NewIntArray(Proc* proc, int length) {
    return SymbolTable::array_of_int_type()->as_array()->form()->NewVal(proc, length);
  }

  static ArrayVal* NewFloatArray(Proc* proc, int length);
  static ArrayVal* NewStringArray(Proc* proc, int length);

  // must be called before any other Factory routine
  static void Initialize(Proc* proc);

 private:
  static BoolVal* bool_t;
  static BoolVal* bool_f;
};

}  // namespace sawzall
