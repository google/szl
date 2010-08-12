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


// This test runs a small Sawzall program and tests the debugger support
// in the Sawzall engine. It steps through the program and checks the file
// name, line number, and function name after each step.

const char* program = {
  // A function that returns something.
  /* 1*/ "positive: function(n: int): bool {\n"
  /* 2*/ "  if (n > 0)\n"
  /* 3*/ "    return true;\n"
  /* 4*/ "  else\n"
  /* 5*/ "    return false;\n"
  /* 6*/ "};\n"
  // A function with no return type.
  /* 7*/ "print: function(n: int) {\n"
  /* 8*/ "  emit stdout <- format(\"The number is %d\", n);\n"
  /* 9*/ "};\n"
  /*10*/ "pos: int = 0;\n"
  /*11*/ "neg: int = 0;\n"
  /*12*/ "for (i: int = 0; i < 4; i++) {\n"
  /*13*/ "  pos = pos + i;\n"
  /*14*/ "  neg = neg - i;\n"
  /*15*/ "}\n"
  /*16*/ "print(pos);\n"
  /*17*/ "zero: int = pos + neg;\n"
  /*18*/ "yes: bool = positive(pos);\n"
  /*19*/ "no: bool = positive(neg);\n"
};


struct Location {
  int line;
  const char* function;
};


struct Location expected_step_locations[] = {
  { 1, "$main" },
  { 7, "$main" },
  { 10, "$main" },
  { 11, "$main" },
  { 12, "$main" },
  { 13, "$main" },
  { 14, "$main" },
  { 12, "$main" },
  { 13, "$main" },
  { 14, "$main" },
  { 12, "$main" },
  { 13, "$main" },
  { 14, "$main" },
  { 12, "$main" },
  { 13, "$main" },
  { 14, "$main" },
  { 12, "$main" },
  { 16, "$main" },
  { 8, "print" },
  { 17, "$main" },
  { 18, "$main" },
  { 2, "positive" },
  { 3, "positive" },
  { 18, "$main" },
  { 19, "$main" },
  { 2, "positive" },
  { 5, "positive" },
  { 19, "$main" },
};


static void StepThroughProgram() {
  VLOG(1) << "StepThroughProgram";
  // Compile program.
  sawzall::Executable exe("debugger_test", program, sawzall::kDebugger);
  CHECK(exe.is_executable());
  // Run program by stepping through with debugger.
  sawzall::Process proc(&exe, NULL);
  proc.InitializeOrDie();
  proc.SetupRun();
  sawzall::DebuggerAPI* debugger = proc.debugger();
  int i = 0;
  while (debugger->Step()) {
    int line = debugger->CurrentLineNumber();
    CHECK_EQ(line, expected_step_locations[i].line);
    const char* function = debugger->CurrentFunctionName();
    CHECK_EQ(strcmp(function, expected_step_locations[i].function), 0);
    CHECK_EQ(strcmp(debugger->CurrentFileName(), "debugger_test"), 0);
    i++;
  }
  CHECK(i == sizeof(expected_step_locations)/sizeof(expected_step_locations[0]));
  delete debugger;
}


int main(int argc, char **argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  StepThroughProgram();
  printf("PASS\n");
  return 0;
}
