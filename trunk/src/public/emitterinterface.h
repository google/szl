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

#ifndef _PUBLIC_EMITTERINTERFACE_H_
#define _PUBLIC_EMITTERINTERFACE_H_

#include <string>

namespace sawzall {

class TableInfo;

// An emitter interface to be implemented by each embedding backend.
class Emitter {
 public:
  // Destruction
  virtual ~Emitter() = 0;

  // Groups
  // To emit a group of data (e.g. a tuple), use the Begin() and
  // End() methods to bracket the various PutX (or nested Begin(),
  // End()) calls for each group element:
  //
  //   1) call Begin(<appropriate compound type>, <length>)
  //   2) call the various PutX methods (or emit a nested group)
  //   3) call End(<appropriate compound type>, <length>)
  //
  // - Emits, indices, weights, and elements all represent a group by
  //   themselves.
  // - Indices and weights are optional, elements must always appear.
  // - The Begin() and End() calls take a length parameter. The value
  //   of the length parameter is as follows:
  //   - for INDEX: the number of indices
  //   - for ARRAY: the number of array elements
  //   - for MAP: the number of map entries (= (key, value) pairs)
  //   - for TUPLE: the number of tuple fields
  //   - for all other types the value is 1
  //
  // Call protocol (in EBNF):
  //
  //   Emit = 'Begin(EMIT, 1)' [Index] Element [Weight] 'End(EMIT, 1)'
  //   Index = 'Begin(INDEX, n)' { Value } 'End(INDEX, n)'.
  //   Element = 'Begin(ELEMENT, 1)' Value 'End(ELEMENT, 1)'.
  //   Weight = 'Begin(WEIGHT, 1)' Value 'End(WEIGHT, 1)'.
  //   Value = Basic | Array | Map | Tuple.
  //   Basic = 'PutBool(x)' | 'PutBytes(x)' | ... 'PutTime(x)'.
  //   Array = 'Begin(ARRAY, n)' { Elem } 'End(ARRAY, n)'.
  //   Elem = Value.
  //   Map = 'Begin(MAP, n) { Key Value } 'End(MAP, n)'.
  //   Key = Value.
  //   Tuple = 'Begin(TUPLE, n) { Field } 'End(TUPLE, n)'.
  //   Field = Value.
  //
  // Examples (emit statement followed by corresponding call sequence):
  //
  //   emit table <- 1;
  //   Begin(EMIT, 1) Begin(ELEMENT, 1) PutInt(1) End(ELEMENT, 1) End(EMIT, 1)
  //
  //   emit table <- "foobar" weight 2.5;
  //   Begin(EMIT, 1)
  //     Begin(ELEMENT, 1) PutInt(1) End(ELEMENT, 1)
  //     Begin(WEIGHT, 1) PutFloat(2.5) End(WEIGHT, 1)
  //   End(EMIT, 1)
  //
  //   emit table[1] <- { "foo": 1, "bar": 0 };
  //   Begin(EMIT, 1)
  //     Begin(INDEX, 1) PutInt(1) End(INDEX, 1)
  //     Begin(ELEMENT, 1)
  //       Begin(MAP, 2)
  //         PutString("foo", 3) PutInt(1)
  //         PutString("bar", 3) PutInt(0)
  //       End(MAP, 2)
  //     End(ELEMENT, 1)
  //   End(EMIT, 1)
  //
  //   emit table[2]["foo"] <- { 0.0, { 'a', 'b', 'c' }};
  //   Begin(EMIT, 1)
  //     Begin(INDEX, 2) PutInt(2) PutString("foo") End(INDEX, 2)
  //     Begin(ELEMENT, 1)
  //       Begin(TUPLE, 2)
  //         PutFloat(0.0)
  //         Begin(ARRAY, 3)
  //           PutInt('a') PutInt('b') PutInt('c')
  //         End(ARRAY, 3)
  //       End(TUPLE, 2)
  //     End(ELEMENT, 1)
  //   End(EMIT, 1)

  // All the group types
  enum GroupType { EMIT, INDEX, ELEMENT, WEIGHT, ARRAY, MAP, TUPLE };

  // Use these to bracket a group of elements.
  virtual void Begin(GroupType type, int len) = 0;
  virtual void End(GroupType type, int len) = 0;

  // Putters for all basic types
  virtual void PutBool(bool b) = 0;
  virtual void PutBytes(const char* p, int len) = 0;
  virtual void PutInt(int64 i) = 0;
  virtual void PutFloat(double f) = 0;
  virtual void PutFingerprint(uint64 fp) = 0;
  // s is not 0-terminated
  virtual void PutString(const char* s, int len) = 0;
  virtual void PutTime(uint64 t) = 0;

  // Shorthand putters (do not use the Begin/End protocol).
  // a) Shorthand for
  // Begin(EMIT, 1) Begin(ELEMENT, 1) PutInt(i) End(ELEMENT, 1) End(EMIT, 1)
  virtual void EmitInt(int64 i) = 0;
  // b) Shorthand for
  // Begin(EMIT, 1) Begin(ELEMENT, 1) PutFloat(i) End(ELEMENT, 1) End(EMIT, 1)
  virtual void EmitFloat(double f) = 0;
};


// An emitter factory interface to be implemented by each embedding backend.
// Such a factory encapsulates the logic for creating Emitter objects and is to
// be passed to Sawzall's Process, so that the backend emitter installation can
// be requested at run-time.
class EmitterFactory {
 public:
  virtual ~EmitterFactory() = 0;
  virtual Emitter* NewEmitter(TableInfo* table_info, string* error) = 0;
};

// Have to define these outside the class because they are pure virtual.
inline Emitter::~Emitter() { }
inline EmitterFactory::~EmitterFactory() { }

}  // namespace sawzall

#endif // _PUBLIC_EMITTERINTERFACE_H_
