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

// Verifies that intrinsics with non-void return values still halt sawzall
// program execution on error in the same way that assert() does.

#include <stdio.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "public/sawzall.h"
#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/type.h"
#include "engine/proc.h"
#include "engine/node.h"
#include "engine/symboltable.h"


namespace sawzall {

const char* my_failing_void_intrinsic_doc =
"Like a no-arg assert().  Always fails.";
const char* my_failing_void_intrinsic(Proc* proc, Val**& sp) {
  proc->set_error();
  return "my_failing_void_intrinsic() failed";
}

const char* my_failing_int_intrinsic_doc =
"Like a no-arg assert().  Purports to return an int, but always fails.";
const char* my_failing_int_intrinsic(Proc* proc, Val**& sp) {
  proc->set_error();
  return "my_failing_int_intrinsic() failed";
}

void InitializeAssertionUnittestIntrinsics() {
  CHECK(sawzall::SymbolTable::is_initialized());
  sawzall::Proc* proc = sawzall::Proc::initial_proc();

  FunctionType* my_failing_void_intrinsic_functiontype =
      FunctionType::New(proc);
  SymbolTable::RegisterIntrinsic("my_failing_void_intrinsic",
                                 my_failing_void_intrinsic_functiontype,
                                 &my_failing_void_intrinsic,
                                 my_failing_void_intrinsic_doc,
                                 Intrinsic::kNormal);

  FunctionType* my_failing_int_intrinsic_functiontype =
      FunctionType::New(proc)->res(SymbolTable::int_type());
  SymbolTable::RegisterIntrinsic("my_failing_int_intrinsic",
                                 my_failing_int_intrinsic_functiontype,
                                 &my_failing_int_intrinsic,
                                 my_failing_int_intrinsic_doc,
                                 Intrinsic::kNormal);
}


bool RunFails(const char* program, int mode) {
  Executable exe("foo", program, mode);
  CHECK(exe.is_executable());
  Process process(&exe, false, NULL);
  process.InitializeOrDie();
  return !process.Run();
}


// kNormal and kNative agreed on these.
const char kAssertionFailureProgram[] = "assert(false);";
const char kVoidIntrinsicFailureProgram[] = "my_failing_void_intrinsic();";

// However, kNormal's process.Run() failed on this, while kNative's process.Run()
// succeeded.
const char kIntIntrinsicFailureProgram[] = "i: int=my_failing_int_intrinsic();";


int RunTest() {
  int failures = 0;
  int modes[] = {kNormal, kNative};
  const char* programs[] = {kAssertionFailureProgram,
                            kVoidIntrinsicFailureProgram,
                            kIntIntrinsicFailureProgram};
  for (int mode = 0; mode < ARRAYSIZE(modes); ++mode) {
    for (int program = 0; program < ARRAYSIZE(programs); ++program) {
      if (!RunFails(programs[program], modes[mode])) {
        printf("Did not catch fail on program %d mode %d\n", program, mode);
        failures++;
      }
    }
  }
  if (failures == 0) {
    puts("PASS");
    return 0;
  } else {
    puts("FAIL");
    return 1;
  }
}

}  // namespace sawzall

REGISTER_MODULE_INITIALIZER(AssertionUnittest, {
  REQUIRE_MODULE_INITIALIZED(Sawzall);
  sawzall::InitializeAssertionUnittestIntrinsics();
});


int main(int argc, char **argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  return sawzall::RunTest();
}
