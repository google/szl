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
#include "public/commandlineflags.h"
#include "public/logging.h"

#include "public/sawzall.h"

#define COUNTOF(x) (sizeof(x) / sizeof(x[0]))

// This unit test compiles and runs several small Sawzall programs
// many times and thus tests the library's robustness within a long
// running environment such as a server. After some warm-up phase,
// memory use should reach a steady state and not increase any more.
// To test, increase the number of test cycles via the iterations flag.
DEFINE_int32(iterations, 100, "number of compile & run cycles");


const char* programs[] = {
  // 0
  "x: int = 0;",
  // 1
  "fact: function(n: int): int {"
  "  if (n > 0)"
  "    return fact(n-1);"
  "  else"
  "    return 1;"
  "};",
  // 2
  "type A = {a: float @ 1, b: int @ 2, c: fingerprint @ 3};"
  "x: A = {12.3, -4, fingerprint(98773663663664)};"
  "type B = {s0: bytes @ 1, s1: array of A @ 2};"
  "z: B = {bytes(\"hi there\"), {}};",
  // 3
  "fibo: function(n: int): int {"
  "  if (n > 2)"
  "    return fibo(n-1) + fibo(n-2);"
  "  else"
  "    return n;"
  "};"
  ""
  "enum: function(n: int) {"
  "  if (n >= 0) {"
  "    enum(n-1);"
  "  }"
  "};"
  ""
  "enum(10);",
  // 4
  "s: string = \"merry go round\";"
  "for (i: int = 0; i < len(s); i++) {"
  "  t: int = s[0];"
  "  s[0 : $ - 1] = s[1 : $];"
  "  s[$-1] = t;"
  "}"
};


static void RunTestsOnStack() {
  VLOG(1) << "RunTestsOnStack";
  for (int i = FLAGS_iterations; i-- > 0; ) {
    for (int j = COUNTOF(programs); j-- > 0; ) {
      // compile program
      sawzall::Executable exe("multiexe_unittest", programs[j], sawzall::kNormal);
      CHECK(exe.is_executable());
      // run program
      sawzall::Process process(&exe, false, NULL);
      process.InitializeOrDie();
      process.RunOrDie();
    }
  }
}


static inline void swap(sawzall::Process*& a, sawzall::Process*& b) {
  sawzall::Process* t = a;
  a = b;
  b = t;
}


static void RunToCompletion(sawzall::Process* processes[], int n) {
  const int time_slice = 10;  // execute 10 instructions in each time slice
  while (n > 0) {  // as long as there is work to do
    for (int i = n; i-- > 0; ) {  // give each process a time slice
      const bool done = processes[i]->Execute(time_slice, NULL);
      if (done) {
        // the i'th process has terminated
        CHECK(processes[i]->error_msg() == NULL) << ":  execution terminated abnormaly";
        // remove process from process pool by swapping it "out" (order is not relevant)
        n--;  // we have one less process to handle
        swap(processes[i], processes[n]);
      }
    }
  }
}


// Create various numbers of executables and keep them around
// for some time. Iterate trough the available executables and
// execute them, in "parallel".
// Goal: Uncover memory leaks or memory stumpers.
static void RunTestsOnHeap() {
  VLOG(1) << "RunTestsOnHeap";
  // initialize executables
  sawzall::Executable* exes[COUNTOF(programs)];
  for (int i = COUNTOF(programs); i-- > 0; ) {
    exes[i] = NULL;  // not strictly necessary, be conservative
  }
  int n = 0;  // number of executables available
  bool add = true;  // adding vs. removing executables
  // create & run executables
  for (int i = FLAGS_iterations; i-- > 0; ) {
    // compile or delete an executable every one in a while
    if (i % 7 == 0) {
      if (add) {
        // add an executable if we still can
        if (n < COUNTOF(programs)) {
          exes[n] = new sawzall::Executable("multiexe_unittest", programs[n], sawzall::kNormal);
          n++;
        } else {
          assert(n == COUNTOF(programs));
          add = false;
        }
      } else {
        // remove an executable if we still can
        if (n > 0) {
          n--;
          delete exes[n];
          exes[n] = NULL;  // not strictly necessary, be conservative
        } else {
          assert(n == 0);
          add = true;
        }
      }
    }
    // run programs
    if (n > 0) {
      // run several programs in "parallel"
      const int p = 10;  // parallelism
      // create p Sawzall processes
      sawzall::Process* processes[p];
      for (int j = p; j-- > 0; ) {
        processes[j] = new sawzall::Process(exes[(i + j) % n], false, NULL);
      }
      // initialize all processes
      for (int j = p; j-- > 0; ) {
        processes[j]->SetupInitialization();
      }
      RunToCompletion(processes, p);
      // run all processes
      for (int j = p; j-- > 0; ) {
        processes[j]->SetupRun();
      }
      RunToCompletion(processes, p);
      // delete all processes
      for (int j = p; j-- > 0; ) {
        delete processes[j];
        processes[j] = NULL;  // not strictly necessary, be conservative
      }
    }
  }
}


int main(int argc, char **argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();
  VLOG(1) << "iterations = " << FLAGS_iterations;
  RunTestsOnStack();
  RunTestsOnHeap();
  printf("PASS\n");
  return 0;
}
