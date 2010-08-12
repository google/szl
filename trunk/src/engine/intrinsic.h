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

class Intrinsics {
 public:
  // initialization
  static void Initialize();

  // map variable type intrinsics to target functions
  static Intrinsic::CFunction TargetFor(Proc* proc, Intrinsic* fun,
                                        const List<Expr*>* args);

  // special intrinsics
  static const char* Match(Proc* proc, Val**& sp, void* pattern);
  static const char* Matchposns(Proc* proc, Val**& sp, void* pattern);
  static const char* Matchstrs(Proc* proc, Val**& sp, void* pattern);
  static const char* Saw(Proc* proc, Val**& sp, int regex_count, void** cache);
};


// TODO: these definitions that manipulate TupleFields should be wrapped
// up as part of any mechanism to have dynamically loaded libraries. In
// the meantime, for lack of a better place, they sit here.

// helper data structures for initializing tuple types defined in intrinsics

enum TypeID {
  TypeArrayOfInt,
  TypeArrayOfFloat,
  TypeBool,
  TypeBytes,
  TypeFloat,
  TypeInt,
  TypeString,
  TypeTime,
  MaxTypeID
};

extern Type* type_of[MaxTypeID];

struct TupleField {
  const char* name;
  TypeID      id;
};

TupleType* define_tuple(Proc* proc, const char* name, const TupleField* tuplefield, int* index, const int n);

// various helper functions for tuple slot writing
void WriteIntSlot(Proc* proc, TupleVal* t, int index, szl_int value);
void WriteFloatSlot(Proc* proc, TupleVal* t, int index, szl_float value);
void WriteTimeSlot(Proc* proc, TupleVal* t, int index, szl_time value);
void WriteBoolSlot(Proc* proc, TupleVal* t, int index, bool value);
void WriteStringSlot(Proc* proc, TupleVal* t, int index, szl_string value);
void WriteBytesSlot(Proc* proc, TupleVal* t, int index, szl_string value);
void WriteArrayOfIntSlot(Proc* proc, TupleVal* t, int index, const int* a, int len);

}  // namespace sawzall
