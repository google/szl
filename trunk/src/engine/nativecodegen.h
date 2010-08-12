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

class NLabel;

// ----------------------------------------------------------------------------
// NCodeGenState

class NCodeGenState {
 public:
  NCodeGenState()
  : is_lhs_(false),
    is_load_(false),
    delta_(0),
    ttarget_(NULL),
    ftarget_(NULL) {
  }

  NCodeGenState(bool is_lhs, bool is_load, int delta, NLabel* ttarget, NLabel* ftarget)
  : is_lhs_(is_lhs),
    is_load_(is_load),
    delta_(delta),
    ttarget_(ttarget),
    ftarget_(ftarget) {
  }

  bool is_lhs() const  { return is_lhs_; }
  bool is_load() const  { return is_load_; }
  int delta() const  { return delta_; }
  NLabel* ttarget() const  { return ttarget_; }
  NLabel* ftarget() const  { return ftarget_; }

 private:
  bool is_lhs_;
  bool is_load_;
  int delta_;  // != 0 for inc
  NLabel* ttarget_;
  NLabel* ftarget_;
};


// ----------------------------------------------------------------------------
// The code generator

typedef List<TableInfo*> OutputTables;

class NCodeGen: public NodeVisitor {
 private:
  struct FunPtr;    // raw function pointer type plus minimal param info
  class CFunPtr;    // special case: CFunction, pass_proc, 2 args, not vargs
  template<typename T> class ChkFunPtr;
  class FunctionCall;
 public:
  explicit NCodeGen(Proc* proc, const char* source, bool debug);
  virtual ~NCodeGen();

  // Variable offset allocation
  static size_t AllocateStaticOffsets(SymbolTable* symbol_table);
  static void AllocateFrameOffsets(Proc* proc, Function* fun);

  // Code generation
  void GenerateTrapHandlerStubs();
  void GenerateInitializers(SymbolTable* symbol_table,
                            OutputTables* tables, size_t statics_size);
  void GenerateFunction(Statics* statics, Function* fun);

  // Accessors
  int error_count() const  { return error_count_; }
  Instr* code_buffer() const  { return asm_.code_buffer(); }
  int emit_offset() const  { return asm_.emit_offset(); }
  int esp_offset() const  { return asm_.esp_offset(); }
  int stack_height() const  { return stack_height_; }
  void set_stack_height(int height)  { stack_height_ = height; }
  FunctionCall* function_call() const  { return function_call_; }
  void set_function_call(FunctionCall* fc)  { function_call_ = fc; }
  List<TrapDesc*>* trap_ranges() const  { return trap_ranges_; }
  List<Node*>* line_num_info() const  { return line_num_info_; }
  const RegsState& regs()  const  { return regs_; }

  // Allocate a new label (used by Function Node - class Label is opaque)
  static NLabel* NewLabel(Proc* proc);

  // associating code with source
  const RawSource* source() const  { return &source_; }

  // Helpers used for function calls
  size_t ReserveCallArea(int num_args, const RegsState* arg_regs, RegsState* saved_regs);
  void ReleaseCallArea(size_t esp_adjust, const RegsState* saved_regs);
  void CallFunPtr(Operand* fun_ptr, int num_args, bool has_vargs);
  void CallSzlFun(Expr* szl_fun, int num_args);
  void SetupFunctionResult(Type* result_type, bool check_err);
  void PushProc();  // push proc pointer as parameter onto native stack
  static FunPtr BinarySupportFunction(Opcode op);

 private:
  Proc* proc_;
  const RawSource source_;  // the raw source, for code excerpt strings
  Assembler asm_;
  bool debug_;
  int error_count_;
  Tracer tlevel_;
  Expr* expr_;  // current top level expression node
  Statement* statement_;  // current innermost statement node
  FunctionCall* function_call_;  // current calling area being setup

  // Register bookkeeping
  AddrMod GetReg(RegSet rs);  // get a free register from the given set
  AddrMod GetBP(int level, RegSet rs);  // for lexically scoped variables
  AddrMod GetISPAddr(RegSet rs);  // get the address of the interpreter stack pointer
  void ReserveRegs(const Operand* n);  // reserve the registers used by the given operand
  void ReleaseRegs(const Operand* n);  // release the registers used by the given operand

  // IA-32 code generation with register bookkeeping
  void LoadOperand(Operand* n, RegSet rs);  // load the operand in a reg of rs
  void LoadOperandEA(Operand* n, RegSet rs);  // load the operand address in a reg of rs
  void PushOperand(Operand* n);  // push operand on native stack
  void IPushOperand(Operand* n);  // push operand on interpreter stack
  void ReleaseOperand(Operand* n);  // release the regs used by this operand and adjust its ref count if necessary
  void IncRefOperand(Operand* n, RegSet rs);
  void DecRefOperand(Operand* n, RegSet rs);
  void TrapIfUndefOperand(Operand* n, bool pass_info);
  void TrapIfOperandNotNull(Operand* trap_info, bool fatal, AddrMod isp, int num_args);
  void TrapIfInfo(bool fatal);
  void Trap(const Operand* trap_info, bool fatal, AddrMod isp, int num_args);

  // other compilation state
  int stack_height_;  // current interpreter stack height
  bool do_statics_;
  OutputTables* tables_;
  TrapDesc* current_trap_range_;  // the currently open trap range
  List<TrapDesc*>* trap_ranges_;  // the list of collected trap ranges
  List<Node*>* line_num_info_;  // the node list used to generate line number info
  Function* function_;  // the function currently compiled
  Scope* emit_scope_;  // scope for format(), proc(), and file() in emit
  VarDecl* emit_var_;  // output variable bound to emit scope
  int padding_offset_;  // offset of the code in the prologue padding the stack
  NCodeGenState* state_;
  Operand x_;  // native operand representing the current node
  RegsState regs_;  // set of live regs and their ref counts
  NLabel* return_;  // the return point (shared)
  NLabel* global_trap_handler_;  // continuation if initialization or return failed
  NLabel* trap_handler_;  // the trap handler stub
  NLabel* trap_handler_with_info_;  // same but with info provided in eax
  NLabel* fatal_trap_handler_;  // the trap handler stub for fatal errors
  NLabel* fatal_trap_handler_with_info_;  // same but with info provided in eax

  // Error handling
  void Error(const char* error_msg);

  // Accessors
  bool do_statics() const  { return do_statics_; }
  int level() const  {
    // function_ is NULL for initializers and finalizers.
    // Eventually, the 'if' will go away because we will
    // put all code into (possibly implicit) functions.
    assert(function_ == NULL || function_->level() > 0);
    // Level for initializers are 0, however, we return 1 so that the static
    // link gets dereferenced to access the globals in the interpreter frame,
    // where they are actually allocated instead of being allocated in the
    // local native frame.
    return function_ == NULL ? 1 : function_->level();
  }

  // state
  bool is_lhs() const  { return state_->is_lhs(); }
  bool is_load() const  { return state_->is_load(); }
  int delta() const  { return state_->delta(); }
  NLabel* ttarget() const  { return state_->ttarget(); }
  NLabel* ftarget() const  { return state_->ftarget(); }

  // Variable addressing
  int var_index(int offset)  { return offset / (signed int)sizeof(Val*); }
  int bp_delta(int level) {
    if (function_ == NULL) {
      assert(level <= 0);
      return 1;  // statics are allocated on the interpreter stack
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

  friend class NTrapHandler;  // needs access to some NCodeGen state

  // Debugging
  void AddLineInfo(Node* x);

  // execution line counting
  void EmitCounter(Node* x);

  // Control flow
  void Bind(Label* L);
  void Branch(Opcode op, Label* L);  // use current operand if conditional
  void BranchShort(Opcode op, Label* L);  // use current operand if conditional
  void BranchShort(Opcode op, Operand* n, Label* L);
  void Branch(Opcode op, Operand* n, Label* L, bool short_branch);

  // Node traversal
  void Visit(Node* x);

  // Expression code
  void Load(Expr* x, bool is_lhs);
  void ProtectAndLoad(Operand* n, Expr* x, bool is_lhs, Operand* nx);
  void PreloadArg(Operand* x, int pos, int num_reg_args, RegsState* arg_regs);
  void PreloadArgs(Operand* x, int xpos, Operand* y, int ypos,
                   int num_reg_args, RegsState* arg_regs);

  // The 'I' prefix in the function names below indicates that they operate on
  // the interpreter stack rather than on the native stack
  void PushExpr(Expr* x, bool is_lhs);
  void IPushExpr(Expr* x);
  void PushExprs(const List<Expr*>* args, int from_arg, int num_args);
  void PushReverseExprs(const List<Expr*>* args, int num_args);
  void PushReverseExprs(Composite* args, int num_args);
  void PushReverseExprs(Composite* args, int from_arg, int num_args);
  void IPushReverseExprs(const List<Expr*>* args, int num_args);
  void PushISPAddr(AddrMod* isp);  // push address of interpreter stack pointer on native stack
  void PushVal(Operand* n);  // push Val* operand on native stack and inc ref
  void IPushVal(Operand* n);
  void IPopVal(Type* type, AddrMod isp, int num_args, bool check_err);  // pop Val* from interpreter stack into x_
  void StoreVal(Operand* dst, Operand* n, bool check_old_val);  // store Val* operand n in dst
  void IncVal(Operand* dst, int delta);
  void UniqVal(Operand* n, Type* type);

  void PushBP(int level);  // push base pointer
  void PushAddr(Operand* n);  // push operand's address on stack
  void LoadConditional(Expr* x, bool is_lhs, Label* ttarget, Label* ftarget);
  void LoadLHS(Expr* x);
  void Store(Expr* x, int delta);
  void StoreVarDecl(VarDecl* var);
  void Compare(Operand* tag, Operand* label, Type* type);
  void CompareInt(Opcode op, Operand* left, Operand* right);
  void CompareBits(Opcode op, Operand* left, Operand* right);
  void CompareSB(Opcode op, Operand* left, Operand* right);  // string, bytes
  void CompareFAMTC(Opcode op, Operand* left, Operand* right);  // float, array, map, tuple, closure

  void Deref(Operand* ptr, Operand* val, size_t val_size, size_t val_offset);
  void BinaryOp(Operand* left, Operand* right, Opcode op,
                size_t val_size, size_t val_offset);  // current operand = left op right
  void IndexSliceNonMap(Expr* var, Expr* beg, Expr* end, int delta,
                     Expr* length_temp, Type* type, FunPtr fun, bool check_err);
  void InlineLenIntrinsic(Operand* operand, Type* type);
  void DiscardResult(Type* type);

  // Statement code
  void Execute(Statement* stat);

  // Function code
  void Prologue(Function* fun, bool is_bottom_frame);
  void Epilogue(Function* fun, bool is_bottom_frame);

  // Composite initializers
  void InitializeArray(Composite* x, int from_args, int num_args);
  void InitializeMap(Composite* x, int from_args, int num_args);
  void InitializeTuple(Composite* x, int from_args, int num_args);

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
};

}  // namespace sawzall
