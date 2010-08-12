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

#include "public/sawzall.h"
#include "public/emitterinterface.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/code.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/tracer.h"
#include "engine/codegen.h"
#include "engine/compiler.h"
#include "engine/outputter.h"
#include "engine/intrinsic.h"
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"

namespace sawzall {

// Sawzall program designed to return various inputs fed by AddInput()

const char* kReturnTwoSzl =
"original: bytes = input;"
"alternate_two: bytes = getadditionalinput(\"alternate_two\");"
"alternate_one: bytes = getadditionalinput(\"alternate_one\");"

"original_emit: table collection of bytes;"
"alternate_one_emit: table collection of bytes;"
"alternate_two_emit: table collection of bytes;"

"emit original_emit <- original;"
"emit alternate_one_emit <- alternate_one;"
"emit alternate_two_emit <- alternate_two;";

// Emitter to receive bytes from sawzall program
class TestEmitter : public sawzall::Emitter {
 public:
  TestEmitter() {}
  ~TestEmitter() {}

  // All other 'puts' do nothing, we only take raw bytes
  virtual void PutBytes(const char* p, int len) {
    data_.append(p, len);
  }
  virtual void Begin(GroupType type, int len) {}
  virtual void End(GroupType type, int len) {}
  virtual void PutBool(bool b) {}
  virtual void PutInt(int64 i) {}
  virtual void PutFloat(double f) {}
  virtual void PutFingerprint(uint64 fp) {}
  virtual void PutString(const char* s, int len) {}
  virtual void PutTime(uint64 t) {}

  virtual void EmitInt(int64 i) {}
  virtual void EmitFloat(double f) {}

  // Accessor for emitted data
  const string& get_data() { return data_; }

 private:
  string data_;
};


// We should be able to pass and retrieve additional inputs
void BasicAddition() {
  // Setup the sawzall process
  Executable* exe = new Executable("none", kReturnTwoSzl,
                                   kNormal | kIgnoreUndefs);
  CHECK(exe->is_executable());
  Process* process = new Process(exe, NULL);
  process->set_memory_limit(0);

  // Prepare to receive emits
  TestEmitter original_emit;
  TestEmitter alternate_one_emit;
  TestEmitter alternate_two_emit;
  TestEmitter garbage;
  const vector<TableInfo*>* tables = exe->tableinfo();
  for (int i = 0; i < tables->size(); ++i) {
    const char* name = (*tables)[i]->name();
    if(strcmp("original_emit", name) == 0)
      process->RegisterEmitter("original_emit", &original_emit);
    else if(strcmp("alternate_one_emit", name) == 0)
      process->RegisterEmitter("alternate_one_emit", &alternate_one_emit);
    else if(strcmp("alternate_two_emit", name) == 0)
      process->RegisterEmitter("alternate_two_emit", &alternate_two_emit);
    else
      process->RegisterEmitter(name, &garbage);
  }

  // Run
  CHECK(process->Initialize());
  const char input[] = "original_content";
  process->SetupRun(input, sizeof(input)-1, NULL, 0);
  process->proc()->AddInput("alternate_one", "alternate_one_content",
                         strlen("alternate_one_content"));
  process->proc()->AddInput("alternate_two", "alternate_two_content",
                         strlen("alternate_two_content"));
  CHECK(process->RunAlreadySetup());

  // Check results
  CHECK_EQ(string("original_content"), original_emit.get_data());
  CHECK_EQ(string("alternate_one_content"), alternate_one_emit.get_data());
  CHECK_EQ(string("alternate_two_content"), alternate_two_emit.get_data());

  delete exe;
  delete process;
}

// We should be able to overwrite additional inputs
void OverwritingInputs() {
  // Setup the sawzall process
  Executable* exe = new Executable("none", kReturnTwoSzl,
                                   kNormal | kIgnoreUndefs);
  CHECK(exe->is_executable());
  Process* process = new Process(exe, NULL);
  process->set_memory_limit(0);

  // Prepare to receive emits
  TestEmitter original_emit;
  TestEmitter alternate_one_emit;
  TestEmitter alternate_two_emit;
  TestEmitter garbage;
  const vector<TableInfo*>* tables = exe->tableinfo();
  for (int i = 0; i < tables->size(); ++i) {
    const char* name = (*tables)[i]->name();
    if(strcmp("original_emit", name) == 0)
      process->RegisterEmitter("original_emit", &original_emit);
    else if(strcmp("alternate_one_emit", name) == 0)
      process->RegisterEmitter("alternate_one_emit", &alternate_one_emit);
    else if(strcmp("alternate_two_emit", name) == 0)
      process->RegisterEmitter("alternate_two_emit", &alternate_two_emit);
    else
      process->RegisterEmitter(name, &garbage);
  }

  // Run
  CHECK(process->Initialize());
  const char input[] = "original_content";
  process->SetupRun(input, sizeof(input)-1, NULL, 0);
  process->proc()->AddInput("alternate_one", "alternate_one_content",
                         strlen("alternate_one_content"));
  process->proc()->AddInput("alternate_two", "alternate_two_content",
                         strlen("alternate_two_content"));
  process->proc()->AddInput("alternate_one", "alternate_one_new_content",
                         strlen("alternate_one_new_content"));
  process->proc()->AddInput("alternate_two", "alternate_two_new_content",
                         strlen("alternate_two_new_content"));
  CHECK(process->RunAlreadySetup());

  // Check results
  CHECK_EQ(string("original_content"), original_emit.get_data());
  CHECK_EQ(string("alternate_one_new_content"), alternate_one_emit.get_data());
  CHECK_EQ(string("alternate_two_new_content"), alternate_two_emit.get_data());

  delete exe;
  delete process;
}

// We should be able to clear additional inputs
void ClearingInputs() {
  // Setup the sawzall process
  Executable* exe = new Executable("none", kReturnTwoSzl,
                                   kNormal | kIgnoreUndefs);
  CHECK(exe->is_executable());
  Process* process = new Process(exe, NULL);
  process->set_memory_limit(0);

  // Prepare to receive emits
  TestEmitter original_emit;
  TestEmitter alternate_one_emit;
  TestEmitter alternate_two_emit;
  TestEmitter garbage;
  const vector<TableInfo*>* tables = exe->tableinfo();
  for (int i = 0; i < tables->size(); ++i) {
    const char* name = (*tables)[i]->name();
    if(strcmp("original_emit", name) == 0)
      process->RegisterEmitter("original_emit", &original_emit);
    else if(strcmp("alternate_one_emit", name) == 0)
      process->RegisterEmitter("alternate_one_emit", &alternate_one_emit);
    else if(strcmp("alternate_two_emit", name) == 0)
      process->RegisterEmitter("alternate_two_emit", &alternate_two_emit);
    else
      process->RegisterEmitter(name, &garbage);
  }

  // Run
  CHECK(process->Initialize());
  const char input[] = "original_content";
  process->SetupRun(input, sizeof(input)-1, NULL, 0);
  process->proc()->AddInput("alternate_one", "alternate_one_content",
                         strlen("alternate_one_content"));
  process->proc()->AddInput("alternate_two", "alternate_two_content",
                         strlen("alternate_two_content"));
  process->proc()->ClearInputs();
  CHECK(process->RunAlreadySetup());

  // Check results
  CHECK_EQ(string("original_content"), original_emit.get_data());
  CHECK_EQ(string(""), alternate_one_emit.get_data());
  CHECK_EQ(string(""), alternate_two_emit.get_data());

  delete exe;
  delete process;
}

// SetupRun() should not cause memory errors if there are additional inputs
void NoMemoryErrorFromSetup() {
  // Setup the sawzall process
  Executable* exe = new Executable("none", kReturnTwoSzl,
                                   kNormal | kIgnoreUndefs);
  CHECK(exe->is_executable());
  Process* process = new Process(exe, NULL);
  process->set_memory_limit(0);

  // Add inputs and then setup, make sure clear doesn't crash after
  CHECK(process->Initialize());
  process->proc()->AddInput("alternate_one", "alternate_one_content",
                         strlen("alternate_one_content"));
  process->proc()->AddInput("alternate_two", "alternate_two_content",
                         strlen("alternate_two_content"));
  const char input[] = "original_content";
  process->SetupRun(input, sizeof(input)-1, NULL, 0);
  process->proc()->ClearInputs();

  delete exe;
  delete process;
}


// Sawzall program that calls setadditionalinput(), and uses
// getadditionalinput to test that this works correctly.

const char* kSetTwoIdentifiers =
  "assert(getadditionalinput(\"1\") == B\"\");"
  "assert(getadditionalinput(\"2\") == B\"\");"

  "setadditionalinput(\"1\", input);"
  "setadditionalinput(\"2\", B\"foobar\");"
  "lockadditionalinput();"

  "assert(getadditionalinput(\"1\") == input);"
  "assert(getadditionalinput(\"2\") == B\"foobar\");"
  "assert(getadditionalinput(\"3\") == B\"\");";

// Sawzall program that tests that setadditionalinput can't be called after
// lockadditionalinput
const char* kTestLockAdditionalIdentifiers =
  "assert(getadditionalinput(\"1\") == B\"\");"

  "setadditionalinput(\"1\", input);"
  "lockadditionalinput();"
  "assert(getadditionalinput(\"1\") == input);"

  "setadditionalinput(\"1\", B\"foobar\");"  // has no effect
  "setadditionalinput(\"2\", B\"foobar\");"  // has no effect
  "assert(getadditionalinput(\"1\") == input);"
  "assert(getadditionalinput(\"2\") == B\"\");";


// We should be able to set additional inputs
void BasicTest() {
  // Setup the sawzall process
  Executable exe("none", kSetTwoIdentifiers, kNormal);
  CHECK(exe.is_executable());
  Process process(&exe, NULL);

  // Run
  CHECK(process.Initialize());
  const char input[] = "some data here";
  process.SetupRun(input, sizeof(input)-1, NULL, 0);
  CHECK(process.RunAlreadySetup());

  // Run a second time to make sure proc->additional_input_ doesn't persist
  // between runs.
  const char input2[] = "some other data here";
  process.SetupRun(input2, sizeof(input2)-1, NULL, 0);
  CHECK(process.RunAlreadySetup());
}

// We shouldn't be able to call setadditionalinput after
// lockadditionalinput
void ErrorTest() {
  // Setup the sawzall process
  Executable exe("none", kTestLockAdditionalIdentifiers, kNormal);
  CHECK(exe.is_executable());
  Process process(&exe, NULL);

  // Run
  CHECK(process.Initialize());
  const char input[] = "some data here";
  process.SetupRun(input, sizeof(input)-1, NULL, 0);
  CHECK(process.RunAlreadySetup());
}


}  // namespace sawzall


int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  sawzall::BasicAddition();
  sawzall::OverwritingInputs();
  sawzall::ClearingInputs();
  sawzall::NoMemoryErrorFromSetup();

  sawzall::BasicTest();
  sawzall::ErrorTest();

  puts("PASS");
  return 0;
}
