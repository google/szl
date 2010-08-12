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

#include <stdio.h>

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
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"


namespace sawzall {

// not static for now so we can be friend of SymbolTable
void ValTest1() {
  F.print("ValTest\n");
  Proc* proc = SymbolTable::init_proc_;
  FileLine* fl = SymbolTable::init_file_line();
  
  // Make some integers
  enum { kNValue = 1000 };
  Val* intval[kNValue];
  // Insert some elements
  for (int i = 0; i < kNValue; i++) {
    intval[i] = SymbolTable::int_type()->int_form()->NewVal(proc, i);
  }

  // Make some bytes
  Val* bytesval[kNValue];
  // Insert some elements
  for (int i = 0; i < kNValue; i++) {
    char* s = F.smprint("<%d>", i);
    bytesval[i] = SymbolTable::bytes_type()->bytes_form()->NewValInit(proc, strlen(s), s);
    free(s);
  }
  // Make some strings
  Val* stringval[kNValue];
  // Insert some elements
  for (int i = 0; i < kNValue; i++) {
    char* s = F.smprint("\"%d\"", i);
    stringval[i] = SymbolTable::string_type()->string_form()->NewValInitCStr(proc, s);
    free(s);
  }

  // ---Arrays
  
  // create an 'array of int' type
  Field* ielem = Field::New(proc, fl, "foo", SymbolTable::int_type());
  ArrayType* aitype = ArrayType::New(proc, ielem);
  // allocate an array with 3 elements
  ArrayVal* aival = aitype->form()->NewValInit(proc, 3, intval[42]);
  
  // print it
  F.print("aival = %V\n", proc, aival);
  
  // create an array of int array type
  Field* elem = Field::New(proc, fl, "foo", aitype);
  ArrayType* aaitype = ArrayType::New(proc, elem);
  // allocate an array with 10 elements
  ArrayVal* aaival = aaitype->form()->NewValInit(proc, 10, aival);
  
  // print it
  F.print("aaival = %V\n", proc, aaival);
  
  // create an 'array of string' type
  Field* selem = Field::New(proc, fl, "foo", SymbolTable::string_type());
  ArrayType* astype = ArrayType::New(proc, selem);
  // allocate an array with 3 elements
  ArrayVal* asval = astype->form()->NewValInit(proc, 3, stringval[42]);
  
  // print it
  F.print("asval = %V\n", proc, asval);
  
  // create an 'array of bytes' type
  Field* yelem = Field::New(proc, fl, "foo", SymbolTable::bytes_type());
  ArrayType* aytype = ArrayType::New(proc, yelem);
  // allocate an array with 3 elements
  ArrayVal* ayval = aytype->form()->NewValInit(proc, 3, bytesval[42]);
  
  // print it
  F.print("ayval = %V\n", proc, ayval);
  
  // --- Maps
  
  // create an 'map[int] of int' type
  MapType* miitype = MapType::New(proc, ielem, ielem);
  // allocate a map
  MapVal* miival = miitype->form()->NewValInit(proc, 0, false);

  const int kNmapentries = 32;  // large enough to force a map resize
  assert((kNmapentries-1)* (kNmapentries-1) < kNValue);
  // Insert some values
  for (int i = 0; i < kNmapentries; i++)
    miival->Insert(proc, intval[i], intval[i*i]);
  
  // Check the values
  for (int i = 0; i < kNmapentries; i++) {
    Val* v = miival->Fetch(intval[i]);
    CHECK(v != NULL);
    CHECK(v->as_int()->val() == i*i);
  }
  
  // Check a failure
  {
    Val *v = miival->Fetch(intval[kNmapentries + 10]);
    CHECK(v == 0);
  }
  
  // print it
  F.print("miival = %V\n", proc, miival);
  
  // create an 'map[string] of string' type
  MapType* msstype = MapType::New(proc, selem, selem);
  // allocate a map
  MapVal* mssval = msstype->form()->NewValInit(proc, 0, false);

  // Insert some values
  for (int i = 0; i < kNmapentries; i++)
    mssval->Insert(proc, stringval[i], stringval[i*i]);
  
  // Check the values
  for (int i = 0; i < kNmapentries; i++) {
    Val* v = mssval->Fetch(stringval[i]);
    CHECK(v != NULL);
    CHECK(v->as_string()->IsEqual(stringval[i*i]));
  }
  
  // create an 'map[bytes] of bytes' type
  MapType* myytype = MapType::New(proc, yelem, yelem);
  // allocate a map
  MapVal* myyval = myytype->form()->NewValInit(proc, 0, false);

  // Insert some values
  for (int i = 0; i < kNmapentries; i++)
    myyval->Insert(proc, bytesval[i], bytesval[i*i]);
  
  // Check the values
  for (int i = 0; i < kNmapentries; i++) {
    Val* v = myyval->Fetch(bytesval[i]);
    CHECK(v != NULL);
    CHECK(v->as_bytes()->IsEqual(bytesval[i*i]));
  }
  
  // Check a failure
  {
    Val *v = myyval->Fetch(bytesval[kNmapentries + 10]);
    CHECK(v == 0);
  }
  
  // print it
  F.print("myyval = %V\n", proc, myyval);
   
  F.print("done\n");
}

}  // namespace sawzall


int main(int argc, char **argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  sawzall::SymbolTable::Initialize();
  sawzall::ValTest1();
  printf("PASS\n");
  return 0;
}
