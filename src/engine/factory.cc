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

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/proc.h"
#include "engine/factory.h"


namespace sawzall {

BoolVal* Factory::bool_t = NULL;
BoolVal* Factory::bool_f = NULL;

void Factory::Initialize(Proc *proc) {
  bool_t = SymbolTable::bool_form()->NewVal(proc, true);
  bool_f = SymbolTable::bool_form()->NewVal(proc, false);
}



StringVal* Factory::NewStringCPP(Proc* proc, const string& x) {
  return SymbolTable::string_type()->string_form()->NewValInitCStr(proc,
                                                                   x.c_str());
}


StringVal* Factory::NewStringBytes(Proc* proc, int length, const char* str) {
  return SymbolTable::string_form()->NewValInit(proc, length, str);
}


TimeVal* Factory::NewTime(Proc* proc, szl_time x) {
  return SymbolTable::time_form()->NewVal(proc, x);
}


ArrayVal* Factory::NewBytesArray(Proc* proc, int length) {
  return SymbolTable::array_of_bytes_type()->as_array()->form()->NewVal(proc, length);
}


ArrayVal* Factory::NewFloatArray(Proc* proc, int length) {
  return SymbolTable::array_of_float_type()->as_array()->form()->NewVal(proc, length);
}


ArrayVal* Factory::NewStringArray(Proc* proc, int length) {
  return SymbolTable::array_of_string_type()->as_array()->form()->NewVal(proc, length);
}


}  // namespace sawzall
