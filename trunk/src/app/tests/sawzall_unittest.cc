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

#include "public/porting.h"
#include "public/logging.h"

#include "public/sawzall.h"


// Verify that we can have several executables w/o scope conflicts;
// e.g., doubly declared predefined identifiers - was bug
static int Test0() {
  static const char* source =
    "i: int = 0;\n"
    "return;\n";
  sawzall::Executable exe0 = sawzall::Executable("<sawzall1>", "", sawzall::kNormal);
  sawzall::Executable exe1 = sawzall::Executable("<sawzall2>", "", sawzall::kDebug);
  sawzall::Executable exe2 = sawzall::Executable("<sawzall3>", source, sawzall::kNormal);
  sawzall::Executable exe3 = sawzall::Executable("<sawzall4>", source, sawzall::kDebug);
  CHECK(exe0.is_executable());
  CHECK(exe1.is_executable());
  CHECK(exe2.is_executable());
  CHECK(exe3.is_executable());
  return 0;
}


int main(int argc, char *argv[]) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  int errors = 0;
  errors += Test0();
  // ... more tests ...

  if (errors == 0)
    printf("PASS\n");

  return errors;
}
