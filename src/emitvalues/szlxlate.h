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

class SzlType;

// An interface for translating szl values into a format understandable by
// other tools.

class SzlXlate {
 public:

  // Is this value type suitable for conversion into a format understandable
  // by other tools?
  static bool IsTranslatableType(const SzlType& type);

  // Can this type be used as a key in, for example, an SSTable?
  // Implies this is a translatable type.
  static bool IsTranslatableKeyType(const SzlType& type);

  // Translate the value in dec into out, and return a suitable sharding
  // value in shardfp, which can be modded by the number of output shards.
  static void TranslateValue(const SzlType& type, SzlDecoder* dec,
                             string *out, uint64* shardfp);

 private:

  // Never instantiated; merely an interface for functions.
  SzlXlate() {}
};
