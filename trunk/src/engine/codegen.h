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

class BLabel;

// ----------------------------------------------------------------------------
// BCodeGenState

class BCodeGenState {
 public:
  BCodeGenState()
  : is_lhs_(false),
    is_load_(false),
    delta_(0),
    ttarget_(NULL),
    ftarget_(NULL) {
  }

  BCodeGenState(bool is_lhs, bool is_load, int delta, BLabel* ttarget, BLabel* ftarget)
  : is_lhs_(is_lhs),
    is_load_(is_load),
    delta_(delta),
    ttarget_(ttarget),
    ftarget_(ftarget) {
  }

  bool is_lhs() const  { return is_lhs_; }
  bool is_load() const  { return is_load_; }
  int delta() const  { return delta_; }
  BLabel* ttarget() const  { return ttarget_; }
  BLabel* ftarget() const  { return ftarget_; }

 private:
  bool is_lhs_;
  bool is_load_;
  int delta_;  // != 0 for inc
  BLabel* ttarget_;
  BLabel* ftarget_;
};


// ----------------------------------------------------------------------------
// The code generator

typedef List<TableInfo*> OutputTables;

class CodeGen: public NodeVisitor {
 public:
  explicit CodeGen(Proc* proc, const char* source, bool debug);
  virtual ~CodeGen();

  // Variable offset allocation
  static size_t AllocateStaticOffsets(SymbolTable* symbol_table);
  static void AllocateFrameOffsets(Function* fun);

  // Code generation
  void GenerateInitializers(SymbolTable* symbol_table, OutputTables* tables,
                            size_t statics_size);
  void GenerateFunction(Statics* statics, Function* fun, bool leave_unreturned);

  // Accessors
  int error_count() const  { return error_count_; }
  Instr* code_buffer() const  { return code_buffer_; }
  int emit_offset() const  { return emit_pos_ - code_buffer_; }
  List<TrapDesc*>* trap_ranges() const  { return trap_ranges_; }

  // Allocate a new label (used by Function Node - class Label is opaque)
  static BLabel* NewLabel(Proc* proc);

  // associating code with source
  void AddLineInfo(Node* x);
  List<Node*>* line_num_info() { return line_num_info_; }
  const RawSource* source() const  { return &source_; }

 private:
  Proc* proc_;
  const RawSource source_;  // the raw source, for code excerpt strings
  bool debug_;
  int error_count_;
  Tracer tlevel_;

  // code buffer
  // invariant: code_buffer_ <= emit_pos_ <= code_limit_
  Instr* code_buffer_;  // the code buffer currently used
  Instr* code_limit_;  // the code buffer limit
  Instr* emit_pos_;  // the position for the next emit
  bool dead_code_;  // if set, code emission is disabled

  // other compilation state
  int max_stack_height_;  // maximum stack height relative to fp
  int stack_height_;  // current stack height relative to fp
  bool do_statics_;
  OutputTables* tables_;
  TrapDesc* current_trap_range_;  // the currently open trap range
  List<TrapDesc*>* trap_ranges_;  // the list of collected trap ranges
  List<Node*>* line_num_info_;  // associate Nodes with the source code
  Function* function_;  // the function currently compiled
  Scope* emit_scope_;  // scope for format(), proc(), and file() in emit
  VarDecl* emit_var_;  // output variable bound to emit scope
  BCodeGenState* state_;
  bool cc_set_;  // true if the condition code is set
  BLabel* global_trap_handler_;  // continuation if initialization of return failed

  static const int kNumElems;  // unit of composite initialization

  // Error handling
  void Error(const char* error_msg);

  // Accessors
  // general
  bool do_statics() const  { return do_statics_; }
  int level() const  {
    // function_ is NULL for initializers and finalizers.
    // Eventually, the 'if' will go away because we will
    // put all code into (possibly implicit) functions.
    assert(function_ == NULL || function_->level() > 0);
    return function_ == NULL ? 0 : function_->level();
  }

  // state
  bool is_lhs() const  { return state_->is_lhs(); }
  bool is_load() const  { return state_->is_load(); }
  int delta() const  { return state_->delta(); }
  BLabel* ttarget() const  { return state_->ttarget(); }
  BLabel* ftarget() const  { return state_->ftarget(); }

  // Variable addressing
  int var_index(size_t offset)  { return offset / sizeof(Val*); }
  int bp_delta(int level) {
    if (function_ == NULL) {
      assert(level <= 0);
      return 0;
    } else {
      return Function::bp_delta(function_, level);
    }
  }

  // Emit statement
  void set_emit_scope(Scope* scope, VarDecl* var) {
    assert(emit_scope_ == NULL && emit_var_ == NULL);
    emit_scope_ = scope;
    emit_var_ = var;
  }

  void reset_emit_scope() {
    emit_scope_ = NULL;
    emit_var_ = NULL;
  }

  // Stack size computation
  void SetStack(int height);  // set the stack height, keep track of maximum height
  void AdjustStack(int delta)  { SetStack(stack_height_ + delta); }

  // Code emission helpers
  friend class StackMark;  // needs access to stack_height_
  friend class TrapHandler;  // needs access to some CodeGen state
  void MakeSpace();
  bool emit_ok();  // returns true if code should be emitted

  // Code emission
  void emit_(Instr x);
  void emit_op(Opcode op);
  void emit_uint8(uint8 x);
  void emit_int8(int8 x);
  void emit_int16(int16 x);
  void emit_int32(int32 x);
  void emit_pcoff(Code::pcoff x);
  void emit_ptr(const void* x);
  void emit_val(Val* x);
  void align_emit_offset();
  void set_int32_at(int offset, int32 x);

  // Debugging
  void Comment(const char* s);

  // Control flow
  void Bind(Label* L);
  void Branch(Opcode op, Label* L);

  // Node traversal
  void Visit(Node* x);

  // Expression code
  void SetBP(int level);  // for lexically scoped variables
  void Load(Expr* x, bool is_lhs);
  void LoadConditional(Expr* x, bool is_lhs, Label* ttarget, Label* ftarget);
  void LoadComposite(Composite* x, int from, int n);
  void LoadLHS(Expr* x);
  void Store(Expr* x, int delta);
  void StoreVar(Variable* var)  { StoreVarDecl(var->var_decl()); }
  void StoreVarDecl(VarDecl* decl);
  void Push(Val* val);
  void PushBool(bool b);
  void PushInt(szl_int i);
  void Dup(Type* type);
  void Pop(Type* type);
  void Compare(Type* type);
  void LenIntrinsic(Expr* var);
  void DiscardResult(Type* type);

  // Statement code
  void Execute(Statement* stat);

  // Visitor functionality
  // expressions
  virtual void DoExpr(Expr* x)  { ShouldNotReachHere(); }
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
  virtual void DoField(Field* x)  { ShouldNotReachHere(); }
  virtual void DoLiteral(Literal* x);
  virtual void DoVariable(Variable* x);
  virtual void DoTempVariable(TempVariable* x);

  // statements
  virtual void DoStatement(Statement* x)  { ShouldNotReachHere(); }
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
  virtual void DoResult(Result* x);
  virtual void DoReturn(Return* x);
  virtual void DoSwitch(Switch* x);
  virtual void DoWhen(When* x);
  virtual void DoLoop(Loop* x);

  // execution count profiling
  void EmitCounter(Node* x);
};

}  // namespace sawzall
