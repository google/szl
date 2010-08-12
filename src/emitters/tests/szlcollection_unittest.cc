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
#include <string>
#include <vector>

#include "public/porting.h"
#include "public/logging.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlnamedtype.h"
#include "public/szltabentry.h"


// Test a simple colection table.
void TestCollection() {
  // Make testing type: table collection of string.
  SzlType t(SzlType::TABLE);
  t.set_table("collection");
  SzlField telem("", SzlType::kString);
  t.set_element(&telem);
  string error;
  CHECK(t.Valid(&error)) << ": " << error;

  // Test creation of the tables.
  SzlTabWriter* wr = SzlTabWriter::CreateSzlTabWriter(t, &error);
  CHECK(wr != NULL) << ": " << error;
  SzlTabEntry* tab1 = wr->CreateEntry("");
  CHECK(tab1 != NULL);
  SzlTabEntry* tab2 = wr->CreateEntry("");
  CHECK(tab2 != NULL);
  delete tab2;
  tab2 = NULL;

  // Collections are trivial: no aggregation, no filtering.
  CHECK(!wr->Aggregates());
  CHECK(!wr->Filters());

  // Collection doesn't merge or flush, so don't need to test that stuff.

  delete tab1;
  delete wr;
}

// Test collection table of map.
void TestCollectionMap() {
  // table collection of {
  //   map[array of int] of string, array of map[string] of int
  // };
  SzlType t(
      SzlNamedTable("collection")
      .Of(SzlNamedTuple()
          .Field(SzlNamedMap()
                 .Index(SzlNamedArray().Of(SzlNamedInt()))
                 .Of(SzlNamedString()))
          .Field(SzlNamedArray()
                 .Of(SzlNamedMap()
                     .Index(SzlNamedString())
                     .Of(SzlNamedInt())))).type());
  string error;
  CHECK(t.Valid(&error)) << ": " << error;

  // Test creation of the tables.
  SzlTabWriter* wr = SzlTabWriter::CreateSzlTabWriter(t, &error);
  CHECK(wr != NULL) << ": " << error;
  SzlTabEntry* tab1 = wr->CreateEntry("");
  CHECK(tab1 != NULL);
  {
    SzlTabEntry* tab2 = wr->CreateEntry("");
    CHECK(tab2 != NULL);
    delete tab2;
  }

  // Collections are trivial: no aggregation, no filtering.
  CHECK(!wr->Aggregates());
  CHECK(!wr->Filters());
  delete wr;
  delete tab1;

  // Collection doesn't merge or flush, so don't need to test that stuff.
}

int main(int argc, char* argv[]) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  TestCollection();
  TestCollectionMap();

  puts("PASS");

  return 0;
}
