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

// Unittest for custom error handler.

#include <stdio.h>

#include "public/porting.h"
#include "public/logging.h"

#include "public/sawzall.h"

namespace sawzall {

// CountErrorHandler counts the number of calls to Err.
class CountErrorHandler: public ErrorHandler {
 public:
  CountErrorHandler() : num_errors(0), num_warnings(0) { }

  virtual void Report(const char* file_name, int line, int offset,
                      bool is_warning, const char* message) {
    if (is_warning)
      ++ num_warnings;
    else
      ++ num_errors;
  }

  int num_errors;
  int num_warnings;
};


void TestCountErrorHandler() {
  struct {
    const char* program;
    int num_warnings;
    int num_errors;
  } test_case[] = {
    { "garbage", 0, 1 },  // Invalid - should give error
    { "x:= int(10);", 1, 0 },  // Should give a warning
    { "x := 10;", 0, 0}  // Should compile successfully with no errors / warnings
  };
  for (int i = 0; i < ARRAYSIZE(test_case); ++i) {
    CountErrorHandler error_handler;
    Executable exe("foo", test_case[i].program, kNative, &error_handler);
    CHECK_EQ(error_handler.num_warnings, test_case[i].num_warnings);
    CHECK_EQ(error_handler.num_errors, test_case[i].num_errors);
  }
}


// LastErrorHandler stores the error/warning message provided to the last Err
// call.
class LastErrorHandler: public ErrorHandler{
 public:
  LastErrorHandler() { }

  virtual void Report(const char* file_name, int line, int offset,
                      bool is_warning, const char* message) {
    last_error = message;
  }

  string last_error;
};


void TestLastErrorHandler() {
  LastErrorHandler error_handler;
  Executable exe("foo", "garbage", kNative, &error_handler);
  CHECK(!error_handler.last_error.empty());
}

}  // namespace sawzall

int main(int argc, char **argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  sawzall::TestCountErrorHandler();
  sawzall::TestLastErrorHandler();
  puts("PASS");
  return 0;
}
