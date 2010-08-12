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

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include "public/porting.h"
#include "public/logging.h"

#include "public/sawzall.h"

// unit tests for the protocol buffer to bytes conversion.
// in programs there are the individual tests.  Each is run
// twice, once with "emit output <- bytes(z);" appended, and once
// with "emit stdout <- string(z);" appended. In checks
// is a program that reads the first binary, and
// also writes to stdout.  The two stdouts have to be equal.

string programs[] = {
  "type A = {a: int @ 1, b: bool @ 2, c: float @ 3};\n"
  "z: A = {17, false, 12.2};\n"
  ,
  "type A = {a: float @ 1, b: int @ 2, c: fingerprint @ 3};\n"
  "x: A = {12.3, -4, fingerprint(98773663663664)};\n"
  "type B = {s0: bytes @ 1, s1: array of A @ 2};\n"
  "z: B = {bytes(\"hi there\"), {x, x, x}};\n"
  ,
  "type A = {a: float @ 1, b: int @ 2, c: fingerprint @ 3};\n"
  "x: A = {12.3, -4, fingerprint(98773663663664)};\n"
  "type B = {s0: bytes @ 1, s1: array of A @ 2};\n"
  "z: B = {bytes(\"hi there\"), {}};\n"
  ,
  ""};

string checks[] = {
  "type A = {a: int @ 1, b: bool @ 2, c: float @ 3};\n"
  "z: A = input;\n"
  ,
  "type A = {a: float @ 1, b: int @ 2, c: fingerprint @ 3};\n"
  "type B = {s0: bytes @ 1, s1: array of A @ 2};\n"
  "z: B = input;\n"
  ,
  "type A = {a: float @ 1, b: int @ 2, c: fingerprint @ 3};\n"
  "type B = {s0: bytes @ 1, s1: array of A @ 2};\n"
  "z: B = input;\n"
  ,
  ""};

static void TestFoo();
static void RunProgram(const string& prog, const char *in, const char *out);

int main(int argc, char **argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  chdir("/tmp");
  TestFoo();
  printf("PASS\n");
  return 0;
}

static string ReadData(const char *fname) {
  int fd = open(fname, O_RDONLY);
  if (fd < 0)
    return "";
  off_t len = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);
  char *buf = new char[len];  // explicitly deallocated
  if (read(fd, buf, len) != len)
    LOG(FATAL) << "failed read " << fname;
  string ans(buf, len);
  delete[] buf;
  return ans;
}

static void TestFoo() {
  for (int i = 0; programs[i].size() > 0; i++) {
    // binary output
    string prog = programs[i] + "emit output <- bytes(z);\n";
    RunProgram(prog, "/dev/null", "binary-output");

    // text output
    prog = programs[i] + "emit stdout <- string(z);\n";
    RunProgram(prog, "/dev/null", "text-output");

    // binary input, check output
    prog = checks[i] + "emit stdout <- string(z);\n";
    RunProgram(prog, "binary-output", "check-output");

    // and check the result
    string one = ReadData("text-output");
    string two = ReadData("check-output");
    CHECK_EQ(one, two);
  }
}


static void RunProgram(const string& prog,const char *in, const char *out) {
  // save old file descriptor
  int fd1 = dup(1);
  close(1);

  int ret = creat(out, 0666);
  CHECK_EQ(ret, 1);

  // get the data (if any)
  string data = ReadData(in);

  // run the program
  sawzall::Executable exe("<test>", prog.c_str(),
                          sawzall::kNormal | sawzall::kIgnoreUndefs);
  sawzall::Process process(&exe, NULL);
  // emitters would be set up here
  process.InitializeOrDie();
  process.RunOrDie(data.data(), data.size(), NULL, 0);

  // put the stdout back
  close(1);
  dup2(fd1, 1);
}
