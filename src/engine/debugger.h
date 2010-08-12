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

namespace sawzall {

// The Debugger class provides methods to control the execution of a Sawzall
// program and to return information about the current state of execution.
// This class is for internal use only. For public access, please see the
// DebuggerAPI class, declared in sawzall.h.

class Debugger {
 public:
  explicit Debugger(Proc* proc);
  ~Debugger();

  // Execute the program. This can be called either before the Sawzall program
  // has started, or when it is stopped after a call to Step().
  void Continue();

  // Execute to the next line, stepping into function calls. Returns false iff
  // the program has terminated (properly or with an error) and cannot be
  // continued.
  bool Step();

  int CurrentLineNumber();
  const char* CurrentFileName();
  const char* CurrentFunctionName();

  // TODO: add support for getting program stack and data

 private:
  void UpdateState();

  Proc* proc_;

  struct {
    Instr* pc_;  // program counter
    Function* function_;
    Statement* statement_;
  } state_;
};


}  // namespace sawzall
