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

// Constant folding.


namespace sawzall {

class ConstantFoldingVisitor : public NodeVisitor {
 public:
  ConstantFoldingVisitor(Proc* proc) : proc_(proc)  { }
 protected:
  // May be used in contexts which have different mechanisms
  // for reporting warnings.  So the client must provide this method.
  virtual void Warning(const FileLine* fileline, const char* fmt, ...) = 0;

  // For most nodes just visit the child nodes.
  virtual void DoNode(Node* x) {
    x->VisitChildren(this);
  }
  // Do not look inside functions.
  virtual void DoFunction(Function* x)  { }

  virtual Expr* VisitBinary(Binary* x);
  virtual Expr* VisitCall(Call* x);
  virtual Expr* VisitConversion(Conversion* x);
  virtual Expr* VisitDollar(Dollar* x);
  virtual Expr* VisitRuntimeGuard(RuntimeGuard* x);
  virtual Expr* VisitIndex(Index* x);
  virtual Expr* VisitNew(New* x);
  virtual Expr* VisitSlice(Slice* x);

  // Regex is folded by the code generators
  // Saw is not practical to fold
  // Selector is not, but could be, folded when applied to a Composite
  // Variable and TempVariable are not foldable, but see SubstitutionVisitor

private:
  Proc* proc_;
};


// In addition to standard constant folding,
// encapsulates optimizations we can do at parse-time for expression
// referencing static variables.
class StaticVarFoldingVisitor : public ConstantFoldingVisitor {
 public:
  StaticVarFoldingVisitor(Proc* proc) : ConstantFoldingVisitor(proc) {}

 private:
  virtual void Warning(const FileLine* fileline, const char* fmt, ...) {}
  virtual Expr* VisitVariable(Variable* x);
};

}  // namespace sawzall
