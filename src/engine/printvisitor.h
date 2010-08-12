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

class PrintNodeVisitor: public NodeVisitor {
 public:
  PrintNodeVisitor(Fmt::State* f, int indent);

  static int NodeFmt(Fmt::State* f);

  // expressions
  virtual void DoBadExpr(BadExpr* x);
  virtual void DoBinary(Binary* x);
  virtual void DoCall(Call* x);
  virtual void DoComposite(Composite* x);
  virtual void DoConversion(Conversion* x);
  virtual void DoDollar(Dollar* x);
  virtual void DoFunction(Function* x);
  virtual void DoSelector(Selector* x);
  virtual void DoRuntimeGuard(RuntimeGuard* x);
  virtual void DoIndex(Index* x);
  virtual void DoNew(New* x);
  virtual void DoRegex(Regex* x);
  virtual void DoSaw(Saw* x);
  virtual void DoSlice(Slice* x);
  virtual void DoStatExpr(StatExpr* x);
  virtual void DoField(Field* x);
  virtual void DoIntrinsic(Intrinsic* x);
  virtual void DoLiteral(Literal* x);
  virtual void DoTypeName(TypeName* x);
  virtual void DoVariable(Variable* x);
  virtual void DoTempVariable(TempVariable* x);

  // statements
  virtual void DoAssignment(Assignment* x);
  virtual void DoBlock(Block* x);
  virtual void DoBreak(Break* x);
  virtual void DoContinue(Continue* x);
  virtual void DoTypeDecl(TypeDecl* x);
  virtual void DoVarDecl(VarDecl* x);
  virtual void DoEmit(Emit* x);
  virtual void DoEmpty(Empty* x);
  virtual void DoExprStat(ExprStat* x);
  virtual void DoIf(If* x);
  virtual void DoIncrement(Increment* x);
  virtual void DoProto(Proto* x);
  virtual void DoResult(Result* x);
  virtual void DoReturn(Return* x);
  virtual void DoSwitch(Switch* x);
  virtual void DoWhen(When* x);
  virtual void DoLoop(Loop* x);

  // used by error.cc
  static int TabFmt(Fmt::State* f);
  static int ArgSzlListFmt(Fmt::State* f);

  // the number of bytes emitted by the printer.
  // used by NodeFmt
  int n() const { return n_; }

 private:
  // Helpers
  void P(const char* fmt, ...);
  void DoUnary(Binary* x);  // used for 0-x => -x, etc.
  void ForPart(Statement *stat);

  // State
  Fmt::State* f_;
  int n_;
  int indent_;
};


class PrintTypeVisitor: public TypeVisitor {
 public:
  PrintTypeVisitor(Fmt::State* f, int indent);

  // types
  static  int  TypeFmt(Fmt::State* f);
  virtual void DoType(Type* x)  { ShouldNotReachHere(); }
  virtual void DoArrayType(ArrayType* x);
  virtual void DoBadType(BadType* x);
  virtual void DoBasicType(BasicType* x);
  virtual void DoFunctionType(FunctionType* x);
  virtual void DoIncompleteType(IncompleteType* x);
  virtual void DoMapType(MapType* x);
  virtual void DoOutputType(OutputType* x);
  virtual void DoTupleType(TupleType* x);

  // used by TypeFmt
  int n() const { return n_; }

 private:
  // Helpers
  void P(const char* fmt ...);
  void DoField(Field* f);

  // State
  Fmt::State *f_;
  int n_;
  int indent_;
  bool in_auto_proto_tuple_;
};

}  // namespace sawzall
