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

// Test the measurement of bytes read and skipped by the ProtocolDecoder.
// Bytes are skipped for proto fields that the Sawzall code does not access.

#include <stdio.h>
#include <stdlib.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/frame.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/engine.h"
#include "public/sawzall.h"

namespace sawzall {

class ProtoBytesSkippedTest {
 public:
  void RunTest(void (ProtoBytesSkippedTest::*pmf)()) {
    (this->*pmf)();
  }

  // Tests
  void ReadFirstField();
  void ReadSecondField();
  void ReadThirdField();
  void ReadNoFields();
  void ReadAllFields();

  void TestProgram(const char* prog, int expect_read, int expect_skipped) {
    // run the program
    sawzall::Executable exe("<test>", prog,
                            sawzall::kNormal | sawzall::kIgnoreUndefs);
    sawzall::Process process(&exe, NULL);
    // emitters would be set up here
    process.InitializeOrDie();
    process.RunOrDie();

    CHECK_EQ(process.ProcProtoBytesRead(), expect_read);
    CHECK_EQ(process.ProcProtoBytesSkipped(), expect_skipped);
  }
};


// All of the programs contain the same 4 types of proto buffers, based
// on the proto buffer encoding documentation.
//
// The full message is 23 bytes long.

void ProtoBytesSkippedTest::ReadFirstField() {
  // Read one field and skip 18 bytes.
  const char program[] =
      "type Proto1 = proto { a: int @ 1 };"
      "type Proto2 = proto { b: bytes @ 2 };"
      "type Proto3 = proto { c: Proto1 @ 3 };"
      "type Proto4 = proto { f1: Proto1 @ 1, f2: Proto2 @ 2, f3: Proto3 @ 3 };"
      "message4: Proto4 = X\"0a03089601\" +"
      "                   X\"1209120774657374696e67\" +"
      "                   X\"1a051a03089601\";"
      "message4.f1.a;";

  TestProgram(program, 23, 18);
}

void ProtoBytesSkippedTest::ReadSecondField() {
  // Read one field and skip 12 bytes.
  const char program[] =
      "type Proto1 = proto { a: int @ 1 };"
      "type Proto2 = proto { b: bytes @ 2 };"
      "type Proto3 = proto { c: Proto1 @ 3 };"
      "type Proto4 = proto { f1: Proto1 @ 1, f2: Proto2 @ 2, f3: Proto3 @ 3 };"
      "message4: Proto4 = X\"0a03089601\" +"
      "                   X\"1209120774657374696e67\" +"
      "                   X\"1a051a03089601\";"
      "message4.f2.b;";

  TestProgram(program, 23, 12);
}

void ProtoBytesSkippedTest::ReadThirdField() {
  // Read one field and skip 16 bytes.
  const char program[] =
      "type Proto1 = proto { a: int @ 1 };"
      "type Proto2 = proto { b: bytes @ 2 };"
      "type Proto3 = proto { c: Proto1 @ 3 };"
      "type Proto4 = proto { f1: Proto1 @ 1, f2: Proto2 @ 2, f3: Proto3 @ 3 };"
      "message4: Proto4 = X\"0a03089601\" +"
      "                   X\"1209120774657374696e67\" +"
      "                   X\"1a051a03089601\";"
      "message4.f3.c.a;";

  TestProgram(program, 23, 16);
}

void ProtoBytesSkippedTest::ReadNoFields() {
  // Read no fields and skip 23 bytes.
  const char program[] =
      "type Proto1 = proto { a: int @ 1 };"
      "type Proto2 = proto { b: bytes @ 2 };"
      "type Proto3 = proto { c: Proto1 @ 3 };"
      "type Proto4 = proto { f1: Proto1 @ 1, f2: Proto2 @ 2, f3: Proto3 @ 3 };"
      "message4: Proto4 = X\"0a03089601\" +"
      "                   X\"1209120774657374696e67\" +"
      "                   X\"1a051a03089601\";";

  TestProgram(program, 23, 23);
}

void ProtoBytesSkippedTest::ReadAllFields() {
  // Read all fields and skip no bytes.
  const char program[] =
      "type Proto1 = proto { a: int @ 1 };"
      "type Proto2 = proto { b: bytes @ 2 };"
      "type Proto3 = proto { c: Proto1 @ 3 };"
      "type Proto4 = proto { f1: Proto1 @ 1, f2: Proto2 @ 2, f3: Proto3 @ 3 };"
      "message4: Proto4 = X\"0a03089601\" +"
      "                   X\"1209120774657374696e67\" +"
      "                   X\"1a051a03089601\";"
      "message4.f1.a;"
      "message4.f2.b;"
      "message4.f3.c.a;";

  TestProgram(program, 23, 0);
}

}   // namespace sawzall

int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  typedef sawzall::ProtoBytesSkippedTest Test;
  Test test;
  test.RunTest(&Test::ReadFirstField);
  test.RunTest(&Test::ReadSecondField);
  test.RunTest(&Test::ReadThirdField);
  test.RunTest(&Test::ReadNoFields);
  test.RunTest(&Test::ReadAllFields);

  puts("PASS");
  return 0;
}
