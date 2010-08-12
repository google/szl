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

#include <assert.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/code.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/proc.h"
#include "engine/debugger.h"


namespace sawzall {

// This visitor is used to find the statement for a given code position.
// It is used by the debugger to identify the statement (and therefore, a line
// number) associated with the program counter.
class StatementVisitor : public NodeVisitor {
 public:
  explicit StatementVisitor(int pos) : pos_(pos), statement_at_pos_(NULL) { }

  Statement* statement_at_pos() { return statement_at_pos_; }

 private:
  virtual void DoNode(Node* x);

  int pos_;
  Statement* statement_at_pos_;
};


void StatementVisitor::DoNode(Node* x) {
  Statement* statement = x->AsStatement();
  if (statement != NULL && statement->code_range()->contains(pos_)) {
    // We've found a statement that contains the pos.
    statement_at_pos_ = statement;
    // Now, visit the statement's children to see if we can find a statement
    // with an even narrower code range that contains the pos.
    x->VisitChildren(this);
  }
}


Debugger::Debugger(Proc* proc) {
  proc_ = proc;
  state_.pc_ = NULL;
  state_.function_ = NULL;
  state_.statement_ = NULL;
}


Debugger::~Debugger() {
}


void Debugger::Continue() {
  while (proc_->Execute(kint32max, NULL) < Proc::TERMINATED) {
  }
}

bool Debugger::Step() {
  // Get the current file and line number.
  const char* start_file_name = CurrentFileName();
  int start_line_number = CurrentLineNumber();
  // Execute instructions until we reach a different file/line.
  while (true) {
    if (proc_->Execute(1, NULL) >= Proc::TERMINATED)
      return false;
    UpdateState();
    if (state_.statement_ == NULL) {
      // We are not on a statement, we don't want to stop here. This happens
      // when the pc is on the ret opcode for a function that has no return
      // type. Another trip through the loop will finish the function's
      // execution.
      continue;
    }
    int line_number = state_.statement_->line();
    const char* file_name = state_.statement_->file();
    if (line_number != start_line_number
        || strcmp(file_name, start_file_name) != 0) {
      // We are now on a different file/line than where we started, so the step
      // is complete.
      break;
    }
    // After executing an instruction, we are still on the same file/line. We
    // need to keep executing instructions until we reach a different
    // file/line.
  }
  return true;
}


int Debugger::CurrentLineNumber() {
  UpdateState();
  // Line numbers are 1 based.
  // 0 is returned if we can't identify a Sawzall source code line number for
  // the current program counter.
  // This is a normal condition when we haven't started executing the program
  // yet.
  return (state_.statement_ != NULL) ? state_.statement_->line() : 0;
}


const char* Debugger::CurrentFileName() {
  UpdateState();
  // NULL is returned if we can't identify a Sawzall source code file for the
  // current program counter.
  // This is a normal condition when we haven't started executing the program
  // yet.
  return (state_.statement_ != NULL) ? state_.statement_->file() : NULL;
}


const char* Debugger::CurrentFunctionName() {
  UpdateState();
  return (state_.function_ != NULL) ? state_.function_->name() : NULL;
}


void Debugger::UpdateState() {
  Instr* pc = proc_->pc();
  if (state_.pc_ != pc) {
    state_.pc_ = pc;
    Code* code = proc_->code();
    state_.function_ = code->FunctionForInstr(state_.pc_);
    StatementVisitor visitor(state_.pc_ - code->base());
    state_.function_->body()->VisitChildren(&visitor);
    state_.statement_ = visitor.statement_at_pos();
  }
}


}  // namespace sawzall
