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
#include <set>

#include "public/hash_map.h"

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/analyzer.h"
#include "engine/symboltable.h"


namespace sawzall {


// ----------------------------------------------------------------------------

// An optimization pass to transform assert calls into if statements,
// thereby avoiding the evaluation of assert's second argument unless
// it is used.  Can be disabled by --nooptimize_sawzall_code.

class RewriteAssertsVisitor : public NodeVisitor {
 public:
  RewriteAssertsVisitor(Analyzer* analyzer) : analyzer_(analyzer) { }

 private:
  // For most nodes just visit the child nodes
  virtual void DoNode(Node* x)  { x->VisitChildren(this); }

  // Wrap calls to assert in conditional statements.  Since assert does not
  // return a value, the calls always occur in expression statements.
  virtual Statement* VisitExprStat(ExprStat* x);

  // Main Analyzer object that gives access to Proc.
  Analyzer* analyzer_;
};


// Transforms "assert(cond, msg);" to "if (!cond) assert(false, msg);"
Statement* RewriteAssertsVisitor::VisitExprStat(ExprStat* x) {
  x->VisitChildren(this);

  // Identify calls to assert.
  Call* call = x->expr()->AsCall();
  if (call == NULL)
    return x;

  Intrinsic* intrinsic = call->fun()->AsIntrinsic();
  if (intrinsic == NULL || (strcmp(intrinsic->name(), "assert") != 0))
    return x;

  assert(call->args()->length() == 2);

  // If the condition is a constant, don't optimize.  assert(true) makes no
  // sense unless users intend for the second argument to have side-effects.
  // assert(false) is most likely the result of users rewriting their
  // asserts by hand, so it would be redundant to rewrite it again.
  Expr* condition = call->args()->at(0);
  if (condition == SymbolTable::bool_t() ||
      condition == SymbolTable::bool_f())
    return x;

  // If the message is a literal after constant folding, then don't optimize.
  Expr* message = call->args()->at(1);
  if (message->AsLiteral() != NULL)
    return x;

  Proc* proc = analyzer_->proc();
  FileLine* file_line = x->file_line();

  // Construct a new call with first argument false.
  List<Expr*>* assert_false_args = List<Expr*>::New(proc);
  assert_false_args->Append(SymbolTable::bool_f());
  for (int i = 1; i < call->args()->length(); i++)
    assert_false_args->Append(call->args()->at(i));

  Call* assert_false_call = Call::New(proc, file_line, intrinsic,
                                      assert_false_args);
  ExprStat* assert_false = ExprStat::New(proc, file_line, assert_false_call);

  // Construct an if statement with the negated condition.
  Expr* not_condition = Binary::New(proc, file_line,
                                    SymbolTable::bool_type(),
                                    SymbolTable::bool_f(),
                                    Binary::EQL, eql_bits,
                                    condition);

  return If::New(proc, file_line, not_condition, assert_false,
                 Empty::New(proc, file_line));
}


// ----------------------------------------------------------------------------
//  Analyzer interface to assert optimization.
// ----------------------------------------------------------------------------

void Analyzer::RewriteAsserts() {
  RewriteAssertsVisitor visitor(this);
  symbol_table_->main_function()->Visit(&visitor);
}

}  // namespace sawzall
