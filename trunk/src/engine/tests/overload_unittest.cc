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

// Unit tests for intrinsic overloading

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

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


class OverloadTest {
 public:
  OverloadTest():
      ftype_string(
          FunctionType::New(Proc::initial_proc())
          ->par(SymbolTable::string_type())
          ->res(SymbolTable::bool_type())),
      ftype_string_returns_string(
          FunctionType::New(Proc::initial_proc())
          ->par(SymbolTable::string_type())
          ->res(SymbolTable::string_type())),
      ftype_int(
          FunctionType::New(Proc::initial_proc())
          ->par(SymbolTable::int_type())
          ->res(SymbolTable::bool_type()))
  {
  }

  void SetUp() {
    my_int_spy_ = 0;
    my_wrong_type_spy_ = false;
    my_string_spy_[0] = '\0';
  }

  void RunTest(void (OverloadTest::*pmf)()) {
    SetUp();
    (this->*pmf)();
  }

  // Tests
  void RegisterIntrinsic_DuplicateFails();
  void RegisterIntrinsic_FailsIfDiffersOnlyInReturnType();
  void RegisterIntrinsic_OverloadSucceeds();
  void TestCallCorrectOverload();
  void TestCallCorrectOverload2();

  static void SpyWrongType() {
    my_wrong_type_spy_ = true;
  }

  static void SpyInt(int val) {
    my_int_spy_ = val;
  }

  static void SpyString(StringVal* val) {
    val->c_str(my_string_spy_, sizeof(my_string_spy_));
  }

 protected:
  FunctionType* ftype_string;
  FunctionType* ftype_string_returns_string;
  FunctionType* ftype_int;

  // Spy variables to verify that correct function is called.
  // These get reset by SetUp() function.
  static int my_int_spy_;
  static char my_string_spy_[1024];
  static bool my_wrong_type_spy_;
};


int OverloadTest::my_int_spy_ = 0;
char OverloadTest::my_string_spy_[];
bool OverloadTest::my_wrong_type_spy_ = false;


// functions to register as intrinsics
const char* my_intrinsic_doc = "Always returns true";
const char* my_intrinsic(Proc* proc, Val**& sp) {
  Val* val = Engine::pop(sp);
  if (val->is_int()) {
    OverloadTest::SpyInt(val->as_int()->val());
  } else if (val->is_string()) {
    OverloadTest::SpyString(val->as_string());
  } else {
    OverloadTest::SpyWrongType();
  }
  Engine::push_szl_bool(sp, proc, true);
  return NULL;
}


void OverloadTest::RegisterIntrinsic_DuplicateFails() {

  // names have to be independent, since SymbolTable is global
  const char* intrinsic_name = "intrinsic1";

  SymbolTable::RegisterIntrinsic(
      intrinsic_name, ftype_string, my_intrinsic,
      my_intrinsic_doc, Intrinsic::kCanFold);

  Object* found1 = SymbolTable::universe()->Lookup(intrinsic_name);
  CHECK_NE(static_cast<Object*>(NULL), found1);

  // second registration uses same name and function type, should fail
  int child = fork();
  if (child == 0) {
    // Child - expect to fail
    SymbolTable::RegisterIntrinsic(
        intrinsic_name, ftype_string, my_intrinsic,
        my_intrinsic_doc, Intrinsic::kCanFold);
    exit(0);
  } else {
    // Parent - wait for child and check status.
    int status;
    waitpid(child, &status, 0);
    CHECK(WIFEXITED(status));
  }
}


void OverloadTest::RegisterIntrinsic_FailsIfDiffersOnlyInReturnType() {

  // names have to be independent, since SymbolTable is global
  const char* intrinsic_name = "intrinsic2";

  SymbolTable::RegisterIntrinsic(
      intrinsic_name, ftype_string, my_intrinsic,
      my_intrinsic_doc, Intrinsic::kCanFold);

  Object* found1 = SymbolTable::universe()->Lookup(intrinsic_name);
  CHECK_NE(static_cast<Object*>(NULL), found1);

  // second registration uses same name and function signature, should fail
  int child = fork();
  if (child == 0) {
    // Child - expect to fail
    SymbolTable::RegisterIntrinsic(
        intrinsic_name, ftype_string_returns_string,
        my_intrinsic, my_intrinsic_doc, Intrinsic::kCanFold);
    exit(0);
  } else {
    // Parent - wait for child and check status.
    int status;
    waitpid(child, &status, 0);
    CHECK(WIFEXITED(status));
  }
}


void OverloadTest::RegisterIntrinsic_OverloadSucceeds() {

  // names have to be independent, since SymbolTable is global
  const char* intrinsic_name = "intrinsic3";

  SymbolTable::RegisterIntrinsic(
      intrinsic_name, ftype_string, my_intrinsic,
      my_intrinsic_doc, Intrinsic::kCanFold);

  // second registration has different function type, so overloads
  SymbolTable::RegisterIntrinsic(
      intrinsic_name, ftype_int, my_intrinsic,
      my_intrinsic_doc, Intrinsic::kCanFold);

  // lookup by name fails for now -- ambiguous
  Object* found = SymbolTable::universe()->Lookup(intrinsic_name);
  CHECK_NE(static_cast<Object*>(NULL), found);

  Intrinsic* fun1 = found->AsIntrinsic();
  CHECK_NE(static_cast<Intrinsic*>(NULL), fun1);
  CHECK(ftype_string->IsEqual(fun1->ftype(), true));
  CHECK_EQ((void*)my_intrinsic, (void*)fun1->function());

  Node* node2 = fun1->next_overload();
  CHECK_NE(static_cast<Node*>(NULL), node2);
  Intrinsic* fun2 = node2->AsIntrinsic();
  CHECK_NE(static_cast<Intrinsic*>(NULL), fun2);
  CHECK(ftype_int->IsEqual(fun2->ftype(), true));
  CHECK_EQ((void*)my_intrinsic, (void*)fun2->function());
}


void OverloadTest::TestCallCorrectOverload() {
  Executable exe("<TestCallCorrectOverload>",
                 "emit stdout <- string(intrinsic3(7));",
                 kNormal);
  CHECK(exe.is_executable());

  Process process(&exe, NULL);
  CHECK(process.Initialize());
  CHECK(process.Run());

  CHECK_EQ(7, my_int_spy_);
  CHECK_EQ('\0', my_string_spy_[0]);
  CHECK_EQ(false, my_wrong_type_spy_);
}


void OverloadTest::TestCallCorrectOverload2() {
  Executable exe("<TestCallCorrectOverload2>",
                 "emit stdout <- string(intrinsic2(\"my test string\"));",
                 kNormal);
  CHECK(exe.is_executable());

  Process process(&exe, NULL);
  CHECK(process.Initialize());
  CHECK(process.Run());

  CHECK_EQ(0, my_int_spy_);
  CHECK(strcmp("my test string", my_string_spy_) == 0);
  CHECK_EQ(false, my_wrong_type_spy_);
}

}  // namespace szl


int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  typedef sawzall::OverloadTest Test;
  Test test;
  test.RunTest(&Test::RegisterIntrinsic_DuplicateFails);
  test.RunTest(&Test::RegisterIntrinsic_FailsIfDiffersOnlyInReturnType);
  test.RunTest(&Test::RegisterIntrinsic_OverloadSucceeds);
  test.RunTest(&Test::TestCallCorrectOverload);
  test.RunTest(&Test::TestCallCorrectOverload2);

  puts("PASS");
  return 0;
}
