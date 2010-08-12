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

#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <assert.h>
#include <string>

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/code.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/frame.h"
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/engine.h"
#include "public/sawzall.h"
#include "engine/tracer.h"
#include "engine/assembler.h"
#include "engine/regsstate.h"
#include "engine/nativesupport.h"
#include "engine/nativecodegen.h"
#include "public/emitterinterface.h"
#include "engine/outputter.h"
#include "engine/compiler.h"
#include "engine/convop.h"
#include "engine/intrinsic.h"
#include "engine/codegenutils.h"

DECLARE_bool(szl_bb_count);


namespace sawzall {

// ----------------------------------------------------------------------------
// Labels
//
// NLabels represent branch and call targets during native code generation.

class NLabel: public Label {
 public:
  NLabel(Proc* proc)
    : forward_(proc),
      short_forward_(proc),
      other_(proc),
      target_(-1),
      esp_offset_(0) {
  }
  virtual ~NLabel()  { assert(!is_linked()); }

  // testers and accessors
  bool is_bound() const  { return target_ >= 0; }
  bool is_linked() const  { return forward_.length() > 0 || short_forward_.length() > 0 || other_.length() > 0; }
  int target() const { return target_; }

  // code generation
  void bind_to(int pos, int esp_offset, Instr* base);  // bind NLabel to position pos (relative to base)
  void add_dep(int* dep, int esp_offset);  // register another dependency
  int offset(int pos, int esp_offset, bool short_offset);  // (branch) offset to NLabel from current position pos

  // branch offsets
  enum {
    kOffsetSize = sizeof(int32),
    kShortOffsetSize = sizeof(int8)
  };

 private:
  // Invariant: (forward_.length() + short_forward_.length() > 0) != (target != NULL)
  List<int> forward_;  // list of forward branch positions
  List<int> short_forward_;  // list of short forward branch positions
  List<int*> other_;  // list of other dependencies (usually trap targets)
  int target_;  // branch destination after bind_to()
  int esp_offset_;  // the stack level for this control flow
};


void NLabel::bind_to(int pos, int esp_offset, Instr* base) {
  assert(pos >= 0);
  assert(!is_linked() || esp_offset == esp_offset_);
  // resolve forward references
  for (int i = forward_.length(); i-- > 0; ) {
    int f = forward_[i];
    int offs = pos - f - kOffsetSize;
    CHECK(offs == static_cast<int32>(offs));  // make sure offset fits into code
    *reinterpret_cast<int32*>(&base[f]) = offs;
  }
  forward_.Clear();
  for (int i = short_forward_.length(); i-- > 0; ) {
    int f = short_forward_[i];
    int offs = pos - f - kShortOffsetSize;
    CHECK(offs == static_cast<int8>(offs));  // make sure offset fits into code
    *reinterpret_cast<int8*>(&base[f]) = offs;
  }
  short_forward_.Clear();
  // resolve other dependencies
  for (int i = other_.length(); i--; )
    *other_[i] = pos;
  other_.Clear();
  // bind label
  target_ = pos;
  esp_offset_ = esp_offset;
}


void NLabel::add_dep(int* dep, int esp_offset) {
  assert(dep != NULL);
  assert(!is_linked() || esp_offset == esp_offset_);
  other_.Append(dep);
  esp_offset_ = esp_offset;
}


int NLabel::offset(int pos, int esp_offset, bool short_offset) {
  if (pos == Assembler::kDeadCodeOffset)
    // asking for the offset from some dead code to this label is allowed
    // this simplifies dead code handling in the caller
    return Assembler::kDeadCodeOffset;

  assert(pos >= 0);
  if (is_bound()) {
    // the label's position is known and we can compute the effective offset
    assert(esp_offset == esp_offset_);
    int offs = target_ - pos;
    if (short_offset) {
      offs -= kShortOffsetSize;
      CHECK(offs == static_cast<int8>(offs));  // make sure offset fits into code
      return static_cast<int8>(offs);
    } else {
      offs -= kOffsetSize;
      CHECK(offs == static_cast<int32>(offs));  // make sure offset fits into code
      return static_cast<int32>(offs);
    }
  } else {
    // the label's position is unknown and we need to keep a (forward) reference
    assert(!is_linked() || esp_offset == esp_offset_);
    if (short_offset)
      short_forward_.Append(pos);
    else
      forward_.Append(pos);

    esp_offset_ = esp_offset;
    return 0;
  }
}


// ----------------------------------------------------------------------------
// FunPtr
//
// This class holds a low-level description of a function for FunctionCall.
// For szl functions the class is used directly; for support functions and
// intrinsics, the derived class ChkFunPtr is used; see below.

struct NCodeGen::FunPtr {
  FunPtr()  { }
  explicit FunPtr(Expr* the_szl_fun) {
    non_szl_fun = NULL;
    szl_fun = the_szl_fun;
    pass_proc = true;
    num_args = 2 + szl_fun->type()->as_function()->parameters()->length();
    has_vargs = false;
  }

  void (*non_szl_fun)();
  Expr* szl_fun;
  bool pass_proc;
  int num_args;
  bool has_vargs;
};


// Special case: using CFunction, two args.

class NCodeGen::CFunPtr: public FunPtr {
 public:
  CFunPtr(Intrinsic::CFunction original_fun_ptr) {
    non_szl_fun = reinterpret_cast<void (*)()>(original_fun_ptr);
    szl_fun = NULL;
    pass_proc = true;
    num_args = 2;
    has_vargs = false;
  }
};


// For support functions and intrinsics, this class lets the compiler check
// that a function fun has the expected signature T and captures whether the
// first parameter is Proc*, the number of non-varargs parameters, whether it
// uses varargs, and (explicitly passed) the number of varargs arguments that
// will be passed.
// The constructor is overloaded so that when varargs is used the number of
// varargs arguments can be supplied.  If the wrong kind of function (varargs
// or non-varargs) is passed, the helper classes (InfoVargs or InfoNoVargs)
// will fail, so the presence or absence of varags is also verified.
//
// Since the function signature is supplied as an explicit template type
// argument, if a ChkFunPtr object is initialized with a support
// function that has the wrong signature then there will be a compilation
// error (unfortunately, it will have a somewhat obscure message).
// This is because there is no template type deduction.
//
// The implementation would have been simpler if it had used a set of
// overloaded template functions "ChkFunPtr", eliminating the need for
// Info[No]Vargs", but that would allow omitting the explicit template args
// and so would risk accidentally leaving out the type check (because then
// it would be possible to use template type deduction).

template<typename T>
class NCodeGen::ChkFunPtr : public FunPtr {
 public:
  ChkFunPtr(T* original_fun_ptr) {
    non_szl_fun = reinterpret_cast<void (*)()>(original_fun_ptr);
    szl_fun = NULL;
    pass_proc = InfoNoVargs<T>::pass_proc;
    num_args = InfoNoVargs<T>::num_args;
    has_vargs = false;
  }

  ChkFunPtr(T* original_fun_ptr, int  num_vargs) {
    non_szl_fun = reinterpret_cast<void (*)()>(original_fun_ptr);
    szl_fun = NULL;
    pass_proc = true;
    num_args = InfoVargs<T>::num_args + num_vargs;
    has_vargs = true;
  }

 private:

  // Templates to extract information about function types.

  // Primary template; no definition, must always match a specialization.
  template<class A> struct InfoNoVargs;

  template<class R, class A>
  struct InfoNoVargs<R (Proc* proc, A a)> {
    static const bool pass_proc = true;
    static const int num_args = 2;
  };

  template<class R, class A, class B>
  struct InfoNoVargs<R (Proc* proc, A a, B b)> {
    static const bool pass_proc = true;
    static const int num_args = 3;
  };

  template<class R, class A, class B, class C>
  struct InfoNoVargs<R (Proc* proc, A a, B b, C c)> {
    static const bool pass_proc = true;
    static const int num_args = 4;
  };

  template<class R, class A, class B, class C, class D>
  struct InfoNoVargs<R (Proc* proc, A a, B b, C c, D d)> {
    static const bool pass_proc = true;
    static const int num_args = 5;
  };

  template<class R, class A, class B>
  struct InfoNoVargs<R (A a, B b)> {
    static const bool pass_proc = false;
    static const int num_args = 2;
  };

  template<class R, class A, class B, class C>
  struct InfoNoVargs<R (A a, B b, C c)> {
    static const bool pass_proc = false;
    static const int num_args = 3;
  };

  template<class R, class A, class B, class C,
           class D, class E, class F, class G>
  struct InfoNoVargs<R (A a, B b, C c, D d, E e, F f, G g)> {
    static const bool pass_proc = false;
    static const int num_args = 7;
  };

  // Primary template; no definition, must always match a specialization.
  template<class A> struct InfoVargs;

  template<class R, class A>
  struct InfoVargs<R (Proc* proc, A a, ...)> {
    static const int num_args = 2;
  };

  template<class R, class A, class B>
  struct InfoVargs<R (Proc* proc, A a, B b, ...)> {
    static const int num_args = 3;
  };

  template<class R, class A, class B, class C>
  struct InfoVargs<R (Proc* proc, A a, B b, C c, ...)> {
    static const int num_args = 4;
  };
};

// ----------------------------------------------------------------------------
// FunctionCall
//
// FunctionCall is a (C++) stack-allocated class used to reserve and release a
// calling area on the target stack while maintaining proper stack alignment.
// Upon construction, FunctionCall calls ReserveCallArea, which generates code
// to save live caller-saved registers and adjust the stack pointer.
// The destructor of FunctionCall calls CallFunPtr or CallSzlFun, which generates
// a function call, and calls ReleaseCallArea, which generates code to pop the
// arguments and restore the saved registers.
// A result_type of SymbolTable::bad_type() indicates a non-Sawzall result type
// requiring no reference counting. See set_type function called from
// SetupFunctionResult.

class NCodeGen::FunctionCall {
 public:
  FunctionCall(NCodeGen* cgen, const FunPtr &fun_ptr, const RegsState* arg_regs,
               Type* result_type, bool check_err)
    : cgen_(cgen),
      fun_ptr_(fun_ptr),
      result_type_(result_type),
      check_err_(check_err) {
    arg_pos_ = num_args();
    esp_offset_ = cgen_->esp_offset();
    esp_adjust_ = cgen_->ReserveCallArea(num_args(), arg_regs, &saved_regs_);
    stack_height_ = cgen_->stack_height();
    super_ = cgen_->function_call();
    cgen_->set_function_call(this);
  }

  ~FunctionCall()  {
    if (fun_ptr_.pass_proc)
      cgen_->PushProc();

    if (fun_ptr_.non_szl_fun != NULL) {
      Operand fun_ptr_imm(fun_ptr_.non_szl_fun);
      cgen_->CallFunPtr(&fun_ptr_imm, num_args(), fun_ptr_.has_vargs);
    } else if (fun_ptr_.szl_fun != NULL) {
      cgen_->CallSzlFun(fun_ptr_.szl_fun, num_args());
    } else {
      ShouldNotReachHere();
    }
    cgen_->SetupFunctionResult(result_type_, check_err_);
    cgen_->ReleaseCallArea(esp_adjust_, &saved_regs_);
    cgen_->set_stack_height(stack_height_);
    cgen_->set_function_call(super_);
    assert(esp_offset_ == cgen_->esp_offset());
    assert(arg_pos_ == 0);
  }

  int arg_pos() const  { return arg_pos_; }
  int num_args() const  { return fun_ptr_.num_args; }
  int NextArgPos() { return --arg_pos_; }

 private:
  NCodeGen* cgen_;
  const FunPtr& fun_ptr_;  // address and param info of function to call
  int arg_pos_;  // current argument position, decremented as args are passed
  Type* result_type_;  // function result type
  bool check_err_;   // whether to set kCheckUndef and kCheckNull in result
  size_t esp_adjust_;  // arg popping and esp adjustment after call
  RegsState saved_regs_;  // caller-saved regs to restore after the call
  int esp_offset_;  // esp_offset before and after function call must match
  int stack_height_;  // interpreter stack height
  FunctionCall* super_;  // enclosing nested function call
};


// ----------------------------------------------------------------------------
// Trap support

// NTrapHandler is a stack-allocated class that takes care of collecting
// TrapRanges during native code generation. The trap range is determined
// by when the NTrapHandler is constructed (begin) and destroyed (end).

class NTrapHandler {
 public:
  NTrapHandler(
    NCodeGen* cgen,  // the code generator
    Label* target,  // the target label
    VarDecl* var,   // the decl of the root var of the expression that may trap
    bool is_silent,  // true, if the trap is silent even w/o --ignore_undefs
    Node* x   // the corresponding node (for debugging only)
  );
  ~NTrapHandler();

 private:
  NCodeGen* cgen_;
  TrapDesc* desc_;
};


NTrapHandler::NTrapHandler(NCodeGen* cgen, Label* target, VarDecl* var, bool is_silent, Node* x)
  : cgen_(cgen),
    desc_(NULL) {
  // during initialization, all traps except in def() are fatal
  if (cgen->do_statics() && !is_silent)
    target = cgen->global_trap_handler_;  // override with global handler
  if (x->CanTrap()) {
    // setup a new trap descriptor - we *must* do it in the constructor
    // of NTrapHandler (as opposed to the desctructor) because it must
    // exist when the target label is bound so it can update the trap
    // descriptor's target dependency (we also need it as super trap
    // range for enclosed trap ranges)
    const int begin = cgen->emit_offset();
    // determine variable index and level, if any
    int index = NO_INDEX;
    int delta = 0;
    if (var != NULL) {
      index = cgen->var_index(var->offset());
      assert(index != NO_INDEX);  // always > 0 because of defined bits
      delta = cgen->bp_delta(var->level());
      assert(delta >= 0);
    }
    // setup && collect trap desc
    desc_ =
      TrapDesc::New(cgen->proc_, begin, begin, begin,  // end and target are unknown yet
                    cgen->stack_height(), -cgen->asm_.esp_offset()/sizeof(Val*),
                    var, index, delta, is_silent,
                    cgen->proc_->PrintString("%L: %n",
                                             x->file_line(),
                                             cgen->source(), x),
                    cgen_->current_trap_range_);
    down_cast<NLabel*>(target)->add_dep(&(desc_->target_), cgen->asm_.esp_offset());  // register target dependency
    // setup new current trap range (the old range is stored desc_)
    cgen_->current_trap_range_ = desc_;
    // we do not rely on a particular order of the trap ranges
    // => collect them now since it's convenient
    cgen_->trap_ranges_->Append(desc_);
    // we cannot have any live caller-saved registers at this point, because
    // they cannot be preserved across trap ranges (they may get saved on the
    // native stack, but not restored by the trap handler in case of a trap)
    assert((cgen->regs().live() & RS_CALLER_SAVED) == RS_EMPTY);
  }
}


NTrapHandler::~NTrapHandler() {
  if (desc_ != NULL) {
    // stack heights at the begin and end of a trap range must match
    assert(desc_->native_stack_height() == -cgen_->asm_.esp_offset()/sizeof(Val*));
    assert(desc_->stack_height() == cgen_->stack_height());
    // at this point we know the entire code range
    // => complete the setup of the trap desc
    desc_->end_ = cgen_->emit_offset();
    // restore previous super trap range
    cgen_->current_trap_range_ = desc_->super();
  }
}


// Operand flag definitions, stored in Operand flags field
enum OperandFlag {
  kCheckUndef = 1 << 0,  // undef check is necessary and not performed yet
  kCheckNull = 1 << 1,  // NULL check is necessary and not performed yet
  kRefIncrd = 1 << 2,  // ref count has been incremented already
  kIsSzlVal = 1 << 3,  // operand represents a szl Val*
  kIsIntVal = 1 << 4,  // operand represents a szl IntVal* (could be an smi)
  kIsSmiVal = 1 << 5,  // operand represents a szl smi IntVal*
};


// set flags in operand
static inline void set_flags(Operand* n, int flags) {
  n->flags |= flags;
}


// set flags in operand
static inline void clear_flags(Operand* n, int flags) {
  n->flags &= ~flags;
}


// set flags in operand according to type and value
static inline void set_type(Operand* n, const Type* type) {
  clear_flags(n, kIsSzlVal | kIsIntVal | kIsSmiVal);
  if (type == SymbolTable::bad_type()) {
    // indicates int result type returned from support routines
    n->size = sizeof(int);
  } else if (type != NULL && !type->is_void()) {
    int flags = kIsSzlVal;
    if (type->is_int() || type->is_output()) {
      set_flags(n, kIsIntVal);
      if (n->am == AM_IMM && (n->value & TaggedInts::tag_mask) == TaggedInts::smi_tag)
        set_flags(n, kIsSmiVal);
    }
    set_flags(n, flags);
  }
}


// set var in operand for use with undef traps
static inline void set_var(Operand* n, VarDecl* var) {
  n->var = var;
}


// return true if the given operand needs an undef check
static inline bool needs_undef_check(const Operand* n) {
  return (n->flags & kCheckUndef) != 0;
}


// return true if the given operand needs a NULL check
static inline bool needs_null_check(const Operand* n) {
  return (n->flags & kCheckNull) != 0;
}


// return true if the given operand needs an undef or NULL check
static inline bool needs_check(const Operand* n) {
  return (n->flags & (kCheckUndef | kCheckNull)) != 0;
}


// return true if the given operand had its ref count incremented already
static inline bool is_ref_incrd(const Operand* n) {
  return (n->flags & kRefIncrd) != 0;
}


// return true if the given operand represents a Sawzall Val pointer
static inline bool is_szl_val(const Operand* n) {
  return (n->flags & kIsSzlVal) != 0;
}


// return true if the given operand represents a Sawzall IntVal pointer or smi
static inline bool is_int_val(const Operand* n) {
  return (n->flags & kIsIntVal) != 0;
}


// return true if the given operand represents a Sawzall smi IntVal
static inline bool is_smi_val(const Operand* n) {
  return (n->flags & kIsSmiVal) != 0;
}


// ----------------------------------------------------------------------------
// Implementation of NCodeGen

NCodeGen::NCodeGen(Proc* proc, const char* source, bool debug)
  : proc_(proc),
    source_(source),
    debug_(debug),
    error_count_(0),
    tlevel_("ncodegen"),
    expr_(NULL),
    statement_(NULL),
    function_call_(NULL),

    // setup remaining state
    stack_height_(0),
    do_statics_(false),
    tables_(NULL),
    current_trap_range_(NULL),
    trap_ranges_(List<TrapDesc*>::New(proc)),  // ownership passed to Code object
    line_num_info_(List<Node*>::New(proc)),  // ownership passed to Code object
    function_(NULL),
    emit_scope_(NULL),
    emit_var_(NULL),
    padding_offset_(0),
    state_(NULL),
    return_(NULL),
    global_trap_handler_(NULL),
    trap_handler_(new NLabel(proc)),  // explicitly deallocated
    trap_handler_with_info_(new NLabel(proc)),  // explicitly deallocated
    fatal_trap_handler_(new NLabel(proc)),  // explicitly deallocated
    fatal_trap_handler_with_info_(new NLabel(proc)) {  // explicitly deallocated

  static NCodeGenState state;  // states are read-only
  state_ = &state;
  reset_emit_scope();
}


NCodeGen::~NCodeGen() {
  delete trap_handler_;
  delete trap_handler_with_info_;
  delete fatal_trap_handler_;
  delete fatal_trap_handler_with_info_;
}


void NCodeGen::Error(const char* error_msg) {
  assert(error_msg != NULL);
  fprintf(stderr, "szl: error: %s\n", error_msg);
  error_count_++;
}


// Stack layout constants
enum {
  // The size of Val pointers, registers, return addresses.
  kPtrSize = NFrame::kStackWidth,
  kPtrSizeLog2 = NFrame::kStackWidthLog2,

  // Maximum number of integer parameters passed in registers
  kMaxNumRegParams = NFrame::kMaxNumRegParams,

  // Maximum number of Sawzall user parameters passed in registers (excl. sl and proc)
  kMaxNumRegSzlParams = NFrame::kMaxNumRegSzlParams,

  // The offset of the static link relative to ebp (or to rbp after it is
  // passed in rdi as 1st parameter and saved to memory in the prologue).
  kStaticLinkOffset = NFrame::kStaticLinkIdx*NFrame::kStackWidth,

  // The offset of the proc pointer relative to ebp (or to rbp after it is
  // passed in rsi as 2nd parameter and saved to memory in the prologue).
  kProcPtrOffset = NFrame::kProcPtrIdx*NFrame::kStackWidth,

  // The offset of the first memory-passed user param relative to ebp (or rbp).
  // Subsequent parameters are allocated at higher addresses.
  kParamStartOffset = NFrame::kParamStartIdx*NFrame::kStackWidth,

  // The memory offset relative to ebp (or rbp) just above the location where
  // the first local (or first register-passed user parameter saved in the
  // function prologue) is allocated.
  // Subsequent saved parameters and locals are allocated at lower addresses.
  kLocalEndOffset = NFrame::kLocalEndIdx*NFrame::kStackWidth,

  // The size occupied in the frame by non-Sawzall values, i.e. by frame links
  kFrameLinksSize = NFrame::kNumFrameLinks*NFrame::kStackWidth,
};


// Registers used to pass function integer arguments
// Maps argument position to register
static const AddrMod arg_reg[kMaxNumRegParams] = {
#if defined(__x86_64__)
  AM_EDI, AM_ESI, AM_EDX, AM_ECX, AM_R8, AM_R9,
#endif
};

// Maps argument position to register set
static const RegSet arg_regset[kMaxNumRegParams] = {
#if defined(__x86_64__)
  RS_EDI, RS_ESI, RS_EDX, RS_ECX, RS_R8, RS_R9,
#endif
};

// Maps number of arguments to set of all used registers
static const RegSet all_arg_regset[kMaxNumRegParams + 1] = {
  RS_EMPTY,
#if defined(__x86_64__)
  RS_EDI,
  RS_EDI | RS_ESI,
  RS_EDI | RS_ESI | RS_EDX,
  RS_EDI | RS_ESI | RS_EDX | RS_ECX,
  RS_EDI | RS_ESI | RS_EDX | RS_ECX | RS_R8,
  RS_EDI | RS_ESI | RS_EDX | RS_ECX | RS_R8 | RS_R9,
#endif
};

// The maximum number of elements pushed on the stack for a
// composite constructor.
static const int kMaxNumCompositeElems = 4096;

size_t NCodeGen::AllocateStaticOffsets(SymbolTable* symbol_table) {
  // no user parameters passed to initialization code (init)
  const size_t params_size = ComputeStaticOffsets(symbol_table->statics(), 0, true);
  assert(params_size == 0);
  // locals are actually statics to be allocated on the stack of the interpreter
  // the native frame of init on the native stack will have no locals and only
  // the implicit static link (gp) and proc parameters
  const size_t statics_size = Frame::kStaticStartOffset +
    ComputeStaticOffsets(symbol_table->statics(), Frame::kStaticStartOffset, false);
  return statics_size;
}


void NCodeGen::AllocateFrameOffsets(Proc* proc, Function* fun) {
  // static link and proc are implicit parameters passed to all functions
#if defined(__i386__)
  fun->set_params_size(ComputeLocalOffsets(fun->locals(), kParamStartOffset, true, true));
  fun->set_locals_size(ComputeLocalOffsets(fun->locals(), kLocalEndOffset, false, false));
#elif defined(__x86_64__)
  List<VarDecl*>* reg_params = List<VarDecl*>::New(proc);  // passed in regs
  List<VarDecl*>* mem_params = List<VarDecl*>::New(proc);  // passed on stack
  int num_regs = kMaxNumRegSzlParams;
  for (int i = 0; i < fun->locals()->length(); i++) {
    VarDecl* var = fun->locals()->at(i);
    if (var->is_param()) {
      if (--num_regs >= 0)
        reg_params->Append(var);
      else
        mem_params->Append(var);
    }
  }
  const size_t reg_params_size = ComputeLocalOffsets(reg_params, kLocalEndOffset, true, false);
  const size_t mem_params_size = ComputeLocalOffsets(mem_params, kParamStartOffset, true, true);
  const size_t locals_size = ComputeLocalOffsets(fun->locals(), kLocalEndOffset - reg_params_size, false, false);
  fun->set_params_size(reg_params_size + mem_params_size);
  fun->set_locals_size(locals_size);
  reg_params->Clear();
  FREE(proc, reg_params);
  mem_params->Clear();
  FREE(proc, mem_params);
#endif
}


void NCodeGen::Prologue(Function* fun, bool is_bottom_frame) {
  // See native frame layout description in frame.h

  // Native frame size is aligned to multiple of kStackAlignment bytes:
  //    Function caller is responsible for pushing parameters in blocks of
  //    kStackAlignment bytes.
  //    Current function is responsible for aligning the combined size of
  //    ret_addr, dynamic_link, locals, padding, and callee-saved regs, as well
  //    as the call area, which consists of spilled registers and arguments to
  //    called functions, therefore the following should be true
  //    (ebp + 2*kPtrSize - esp) % kStackAlignment == 0 after pushing the
  //    callee-saved regs. This defines the amount of necessary padding.

  // No locals in init, statics are accessed via static link
  const size_t locals_size = fun != NULL ? fun->locals_size() : 0;

  // The aligned size includes space for return address, dynamic link, saved
  // register-passed parameters, locals, and saved callee-saved registers
  // We additionally save ebp in bottom frames (of init and main) to make stack
  // unwinding easier
  const int num_saved = NFrame::kNumCalleeSaved + (is_bottom_frame ? 1 : 0);
#if defined(__i386__)
  const size_t reg_params_size = 0;
#elif defined(__x86_64__)
  size_t reg_params_size = fun != NULL ? fun->params_size() : 0;
  if (reg_params_size > kMaxNumRegSzlParams*kPtrSize)
    reg_params_size = kMaxNumRegSzlParams*kPtrSize;
#endif
  const int unaligned_size = kFrameLinksSize + reg_params_size + locals_size + num_saved*kPtrSize;
  const int aligned_size = Align(unaligned_size, NFrame::kStackAlignment);
  const int padding_size = aligned_size - unaligned_size;

  // The frame size stored in fun->frame_size_ is defined as the distance
  // between ebp and esp after executing the prolog.
  size_t frame_size = aligned_size - 2*kPtrSize;
  if (fun != NULL)
    fun->set_frame_size(frame_size);
  else
    assert(frame_size == NFrame::kInitFrameSize);

  assert(regs_.live() == RS_EMPTY);
  assert(asm_.esp_offset() == 0);  // 0, but meaningless, frame not setup yet
  assert(stack_height() == 0);

  // setup dynamic link
  asm_.PushReg(AM_EBP);
  asm_.MoveRegReg(AM_EBP, AM_ESP);

#if defined(__x86_64__)
  // save register-passed parameters to memory
  const int num_regs = reg_params_size/kPtrSize + 2;  // 2 for sl and proc
  for (int i = 0; i < num_regs; ++i)
    asm_.PushReg(arg_reg[i]);
#endif

  // initialize locals to zero
  Operand zero(AM_IMM, 0);
  int count = locals_size / (4*kPtrSize);
  if (count >= 1) {
    if (count > 1) {
      Operand imm_count(AM_IMM, count);
      asm_.Load(AM_ECX, &imm_count);
    }
    NLabel loop(proc_);
    asm_.set_esp_offset(0);
    Bind(&loop);
    // unroll initialization of locals to zero, 4 locals per loop iteration
    asm_.Push(&zero);
    asm_.Push(&zero);
    asm_.Push(&zero);
    asm_.Push(&zero);
    if (count > 1) {
      asm_.AddImmReg(AM_ECX, -1);  // dec ecx and set cc for branch
      x_.am = AM_CC;
      x_.cc = CC_NE;
      asm_.set_esp_offset(0);
      BranchShort(branch_true, &loop);
    }
  }
  // initialize remaining locals to zero, up to 3 locals
  for (int i = locals_size % (4*kPtrSize); i > 0; i -= kPtrSize)
    asm_.Push(&zero);

  // The following code will be patched after the function body is generated.
  // Each PUSH intruction saving a callee-saved register will be patched with a
  // NOP instruction if the callee-saved register was never used in the body.
  // The padding size will be adjusted according to the corrected frame size.
  padding_offset_ = asm_.AddImm8Esp(-padding_size);
  asm_.PushRegs(RS_CALLEE_SAVED);  // save all callee-saved regs for now
  regs_.Clear();  // keep track of used registers from here

  if (is_bottom_frame) {
    // we save ebp of the init (or main) frame, because it will be needed
    // to unwind the stack after aborting executing.
    // instead of saving ebp, we could add the framesize to esp in the unwinding
    // stub, however, the same stub is used for init and main, with a different
    // frame size.
    asm_.PushReg(AM_EBP);

    // Remember current esp for stack unwinding
    Operand proc_ptr(AM_BASED + AM_EBP, kPtrSize, kProcPtrOffset);
    asm_.Load(AM_EAX, &proc_ptr);
    Operand native_bottom_esp(AM_BASED + AM_EAX, kPtrSize, Proc::native_bottom_sp_offset());
    asm_.Store(&native_bottom_esp, AM_ESP);
  }
  asm_.set_esp_offset(0);

  // Generate code verifying that esp % 16 == 0 after executing the prologue.
  // Not checked: ebp % 16 == 8 in 32-bit mode
  //              ebp % 16 == 0 in 64-bit mode
#if 0  // Used for debugging only
  NLabel skip(proc_);
  Operand esp(AM_ESP);
  asm_.TestImm(&esp, NFrame::kStackAlignment - 1);
  Operand is_aligned(AM_CC, CC_E);
  BranchShort(branch_true, &is_aligned, &skip);
  asm_.Int3();
  Bind(&skip);
#endif
}


void NCodeGen::Epilogue(Function* fun, bool is_bottom_frame) {
  // must not trash eax which holds result
  assert(asm_.esp_offset() == 0);
  assert(stack_height() == 0);
  size_t locals_size = fun != NULL ? fun->locals_size() : 0;
  size_t params_size = fun != NULL ? fun->params_size() : 0;
#if defined(__x86_64__)
  // We treat saved register-passed parameters as locals
  size_t reg_params_size = params_size;
  if (reg_params_size > kMaxNumRegSzlParams*kPtrSize)
    reg_params_size = kMaxNumRegSzlParams*kPtrSize;
  params_size -= reg_params_size;
  locals_size += reg_params_size;
#endif
  if (params_size > 0) {
    // dec ref counts of params
    Operand after_last_param_addr(AM_BASED + AM_EBP, kPtrSize, params_size);
    asm_.LoadEA(AM_ECX, &after_last_param_addr);
    NLabel loop(proc_);
    Bind(&loop);
    Operand val_ptr(AM_BASED + GetReg(RS_ECX), kPtrSize, kParamStartOffset-kPtrSize);
    set_type(&val_ptr, SymbolTable::int_type());  // could be an int
    set_flags(&val_ptr, kCheckNull);  // no need to check undef
    DecRefOperand(&val_ptr, RS_EDX);
    ReleaseOperand(&val_ptr);
    if (params_size > kPtrSize) {
      asm_.AddImmReg(AM_ECX, -kPtrSize);
      Operand ebp(AM_EBP);
      asm_.CmpRegEA(AM_ECX, &ebp);
      Operand more(AM_CC, CC_NE);
      BranchShort(branch_true, &more, &loop);
    }
  }
  if (locals_size > 0) {
    // dec ref counts of locals
    Operand last_local_addr(AM_BASED + AM_EBP, kPtrSize, -locals_size);
    asm_.LoadEA(AM_ECX, &last_local_addr);
    NLabel loop(proc_);
    Bind(&loop);
    Operand val_ptr(AM_BASED + GetReg(RS_ECX), kPtrSize, kLocalEndOffset);
    set_type(&val_ptr, SymbolTable::int_type());  // could be an int
    set_flags(&val_ptr, kCheckNull);  // no need to check undef
    DecRefOperand(&val_ptr, RS_EDX);
    ReleaseOperand(&val_ptr);
    if (locals_size > kPtrSize) {
      asm_.AddImmReg(AM_ECX, kPtrSize);
      Operand ebp(AM_EBP);
      asm_.CmpRegEA(AM_ECX, &ebp);
      Operand more(AM_CC, CC_NE);
      BranchShort(branch_true, &more, &loop);
    }
  }
  assert(asm_.esp_offset() == 0);
  if (is_bottom_frame) {
    asm_.PopReg(AM_EBP);
    asm_.PopRegs(RS_CALLEE_SAVED);
  } else {
    assert(fun != NULL);
    // we only need to save the callee-saved registers actually used
    RegSet saved_regs = regs_.used() & RS_CALLEE_SAVED;
    // we saved all callee-saved registers in the prologue, patch code:
    // first push instruction follows imm8 at padding_offset_
    asm_.PatchPushRegs(padding_offset_ + 1, RS_CALLEE_SAVED, saved_regs);

    // restore only used callee-saved registers in epilogue
    asm_.PopRegs(saved_regs);

    // correct frame size (reg_params_size is included in locals_size)
    const int unaligned_size = kFrameLinksSize + locals_size + NumRegs(saved_regs)*kPtrSize;
    const int aligned_size = Align(unaligned_size, NFrame::kStackAlignment);
    const int padding_size = aligned_size - unaligned_size;
    asm_.PatchImm8(padding_offset_, -padding_size);
    fun->set_frame_size(aligned_size - 2*kPtrSize);
  }
  asm_.Leave();
  asm_.Ret();
}


void NCodeGen::GenerateTrapHandlerStubs() {
  CHECK(error_count_ == 0) << ": code generator in error state";
  asm_.set_dead_code(false);

  // Generate trap handler stubs calling
  // Proc::Status NSupport::HandleTrap(char* trap_info, bool fatal, NFrame* fp,
  //                                   intptr_t sp_adjust, int native_sp_adjust,
  //                                   Instr*& trap_pc, Val**& trap_sp);
  //
  // The return address (trap_pc) is already on the stack; we pass its address
  // to the trap handler so that it can be updated with the target pc to
  // continue execution.
  // The sp_adjust and native_sp_adjust values are passed as args to this stub
  // via the stack, even in 64-bit mode for more compact code (no rex prefixes).
  // The stack pointer value (trap_sp) passed to HandleTrap is the value of the
  // stack pointer prior to pushing sp_adjust and native_sp_adjust.
  //
  // We need to keep the stack aligned during trap handling, because stack dumps
  // pass var args of type floating point to formatting routines. In 64-bit
  // mode, floating point values are passed via mmx registers and saving them to
  // an unaligned stack causes a segmentation violation.

  NLabel common_trap_handler(proc_);
  asm_.set_esp_offset(0);
  assert(regs_.live() == RS_EMPTY);
  Operand zero(AM_IMM, 0);
  Operand one(AM_IMM, 1);

#if defined(__i386__)
  const AddrMod trap_info_reg = AM_EAX;
  const AddrMod fatal_reg = AM_ECX;
#elif defined(__x86_64__)
  const AddrMod trap_info_reg = GetReg(RS_EDI);  // parameter 1
  const AddrMod fatal_reg = GetReg(RS_ESI);  // parameter 2
#endif

  Bind(fatal_trap_handler_);  // trap_info_reg is ignored (reset to 0)
  asm_.Load(trap_info_reg, &zero);
  Bind(fatal_trap_handler_with_info_);  // trap_info_reg is trap_info
  asm_.Load(fatal_reg, &one);  // fatal
  BranchShort(branch, &common_trap_handler);

  Bind(trap_handler_);  // trap_info_reg is ignored (reset to 0)
  asm_.Load(trap_info_reg, &zero);
  Bind(trap_handler_with_info_);  // trap_info_reg is trap_info
  asm_.Load(fatal_reg, &zero);  // not fatal

  NLabel unwind(proc_);
  Bind(&common_trap_handler);
  {
#if defined(__i386__)
    //  esp + 3*kPtrSize  == trap_sp
    // [esp + 2*kPtrSize] == native_sp_adjust
    // [esp + 1*kPtrSize] == sp_adjust
    // [esp + 0*kPtrSize] == trap_pc, i.e. return address
    // eax == trap_info
    // ecx == fatal
    //
    //    lea edx, [esp + 3*kPtrSize] ;  trap_sp = esp + 3*kPtrSize
    //    and esp, -kStackAlignment ;  frame is now aligned: esp % 16 == 0
    //    sub esp, 8 ;  pad stack frame
    //    push ecx ;  spill ecx and pad stack frame; size = 3 padding + 2 locals + 7 args
    //    push [edx - 3*kPtrSize] ;  trap_pc, will be patched
    //    mov ecx, esp ;  &trap_pc
    //    push edx ;  trap_sp, will be patched
    //    push esp ;  &trap_sp parameter
    //    push ecx ;  &trap_pc parameter
    //    push [edx - 1*kPtrSize] ;  native_sp_adjust parameter
    //    push [edx - 2*kPtrSize] ;  sp_adjust parameter
    //    push ebp ;  fp parameter
    //    push [ecx + 1*kPtrSize] ;  fatal parameter (was spilled)
    //    push eax ;  trap_info parameter
    //    mov eax, &NSupport::HandleTrap
    //    call [eax]
    //    cmp eax, FAILED
    //    be unwind
    //    mov eax, [esp + 8*kPtrSize] ;  patched trap_pc == target_pc
    //    mov esp, [esp + 7*kPtrSize] ;  patched trap_sp == target_sp
    //    jmp [eax] ;  jump to target_pc
    //
    // Cancel automatic esp relative addressing adjustment by adding esp_offset
    Operand trap_sp(AM_BASED + AM_ESP, kPtrSize, 3*kPtrSize + asm_.esp_offset());
    asm_.LoadEA(AM_EDX, &trap_sp);
    Operand align_mask(AM_IMM, -NFrame::kStackAlignment);
    asm_.AndRegEA(AM_ESP, &align_mask);
    asm_.AddImmReg(AM_ESP, -8);
    asm_.PushReg(AM_ECX);
    Operand trap_pc(AM_BASED + AM_EDX, kPtrSize, -3*kPtrSize);
    asm_.Push(&trap_pc);
    asm_.MoveRegReg(AM_ECX, AM_ESP);
    asm_.PushReg(AM_EDX);
    asm_.PushReg(AM_ESP);
    asm_.PushReg(AM_ECX);
    Operand native_sp_adjust(AM_BASED + AM_EDX, kPtrSize, -1*kPtrSize);
    asm_.Push(&native_sp_adjust);
    Operand sp_adjust(AM_BASED + AM_EDX, kPtrSize, -2*kPtrSize);
    asm_.Push(&sp_adjust);
    asm_.PushReg(AM_EBP);
    Operand spilled_fatal(AM_BASED + AM_ECX, kPtrSize, 1*kPtrSize);
    asm_.Push(&spilled_fatal);
    asm_.PushReg(AM_EAX);
    ChkFunPtr<Proc::Status (char* trap_info, bool fatal, NFrame* fp,
                            intptr_t sp_adjust, int native_sp_adjust,
                            Instr*& trap_pc, Val**& trap_sp)>
      fun_ptr(NSupport::HandleTrap);
    Operand fun_ptr_imm(fun_ptr.non_szl_fun);
    CallFunPtr(&fun_ptr_imm, 7, false);
    Operand failed_status(AM_IMM, Proc::FAILED);
    asm_.CmpRegEA(AM_EAX, &failed_status);
    Operand equal(AM_CC, CC_E);
    BranchShort(branch_true, &equal, &unwind);
    Operand patched_trap_pc(AM_BASED + AM_ESP, kPtrSize, 8*kPtrSize + asm_.esp_offset());
    asm_.Load(AM_EAX, &patched_trap_pc);
    Operand target_pc(AM_EAX);
    Operand target_sp(AM_BASED + AM_ESP, kPtrSize, 7*kPtrSize + asm_.esp_offset());
    asm_.Load(AM_ESP, &target_sp);
    asm_.JmpIndir(&target_pc);
#elif defined(__x86_64__)
    //  rsp + 3*kPtrSize  == trap_sp
    // [rsp + 2*kPtrSize] == native_sp_adjust
    // [rsp + 1*kPtrSize] == sp_adjust
    // [rsp + 0*kPtrSize] == trap_pc, i.e. return address
    // rdi == trap_info, parameter 1
    // rcx == fatal, parameter 2
    //
    //    mov r11, [rsp + 0*kPtrSize] ;  trap_pc
    //    mov rcx, [rsp + 1*kPtrSize] ;  sp_adjust, parameter 4
    //    mov r8,  [rsp + 2*kPtrSize] ;  native_sp_adjust, parameter 5
    //    lea r10, [rsp + 3*kPtrSize] ;  trap_sp = rsp + 3*kPtrSize
    //    and rsp, -kStackAlignment ;  frame is now aligned: rsp % 16 == 0
    //    sub rsp, 8 ;  keep frame aligned: 1 padding + 2 locals + 1 mem arg
    //    push r11 ;  trap_pc to be patched
    //    mov r9, rsp ;  &trap_pc, parameter 6
    //    push r10 ;  trap_sp to be patched
    //    push rsp ;  &trap_sp, parameter 7
    //    mov rdx, rbp ;  fp, parameter 3
    //    mov r11, &NSupport::HandleTrap
    //    call [r11]
    //    cmp rax, FAILED
    //    be unwind
    //    mov r11, [rsp + 2*kPtrSize] ;  patched trap_pc == target_pc
    //    mov rsp, [rsp + 1*kPtrSize] ;  patched trap_sp == target_sp
    //    jmp [r11] ;  jump to target_pc
    //
    // Cancel automatic esp relative addressing adjustment by adding esp_offset
    Operand trap_pc(AM_BASED + AM_ESP, kPtrSize, 0*kPtrSize + asm_.esp_offset());
    asm_.Load(AM_R11, &trap_pc);
    Operand sp_adjust(AM_BASED + AM_ESP, kPtrSize, 1*kPtrSize + asm_.esp_offset());
    asm_.Load(GetReg(RS_ECX), &sp_adjust);  // parameter 4
    Operand native_sp_adjust(AM_BASED + AM_ESP, kPtrSize, 2*kPtrSize + asm_.esp_offset());
    asm_.Load(GetReg(RS_R8), &native_sp_adjust);  // parameter 5
    Operand trap_sp(AM_BASED + AM_ESP, kPtrSize, 3*kPtrSize + asm_.esp_offset());
    asm_.LoadEA(AM_R10, &trap_sp);
    Operand align_mask(AM_IMM, -NFrame::kStackAlignment);
    asm_.AndRegEA(AM_ESP, &align_mask);
    asm_.AddImmReg(AM_ESP, -8);
    asm_.PushReg(AM_R11);
    asm_.MoveRegReg(GetReg(RS_R9), AM_ESP);  // parameter 6
    asm_.PushReg(AM_R10);
    asm_.PushReg(AM_ESP);  // parameter 7
    asm_.MoveRegReg(GetReg(RS_EDX), AM_EBP);  // parameter 3
    ChkFunPtr<Proc::Status (char* trap_info, bool fatal, NFrame* fp,
                            intptr_t sp_adjust, int native_sp_adjust,
                            Instr*& trap_pc, Val**& trap_sp)>
      fun_ptr(NSupport::HandleTrap);
    Operand fun_ptr_imm(fun_ptr.non_szl_fun);
    CallFunPtr(&fun_ptr_imm, 7, false);
    Operand failed_status(AM_IMM, Proc::FAILED);
    asm_.CmpRegEA(AM_EAX, &failed_status);
    Operand equal(AM_CC, CC_E);
    BranchShort(branch_true, &equal, &unwind);
    Operand patched_trap_pc(AM_BASED + AM_ESP, kPtrSize, 2*kPtrSize + asm_.esp_offset());
    asm_.Load(AM_R11, &patched_trap_pc);
    Operand target_pc(AM_R11);
    Operand target_sp(AM_BASED + AM_ESP, kPtrSize, 1*kPtrSize + asm_.esp_offset());
    asm_.Load(AM_ESP, &target_sp);
    asm_.JmpIndir(&target_pc);
#endif
  }

  // Unwind stack by setting esp to value saved in init or main
  // esp = proc->native_bottom_sp()
  // and execute epilogue
  Bind(&unwind);
  {
    //  eax == status
    //    mov ecx, [ebp+kProcPtrOffset]
    //    mov esp, [ecx+Proc::native_bottom_sp_offset()]
    //  epilogue:
    //    pop ebp ; was saved in bottom frame for unwind to work
    //    pop callee-saved regs
    //    leave
    //    ret
    Operand proc_ptr(AM_BASED + AM_EBP, kPtrSize, kProcPtrOffset);
    asm_.Load(AM_ECX, &proc_ptr);
    Operand native_bottom_esp(AM_BASED + AM_ECX, kPtrSize, Proc::native_bottom_sp_offset());
    asm_.Load(AM_ESP, &native_bottom_esp);
    asm_.set_esp_offset(0);

    Epilogue(NULL, true);
  }

  // make sure emit_offset is aligned for the next function
  asm_.align_emit_offset();
}


void NCodeGen::GenerateInitializers(SymbolTable* symbol_table,
                                    OutputTables* tables, size_t statics_size) {
  CHECK(error_count_ == 0) << ": code generator in error state";
  do_statics_ = true;
  tables_ = tables;
  function_ = NULL;
  asm_.set_esp_offset(0);
  asm_.set_dead_code(false);
  assert(stack_height() == 0);

  // add a line info entry to record the start of the initialization code
  Empty* init_line_info = Empty::New(proc_, SymbolTable::init_file_line());
  AddLineInfo(init_line_info);
  int beg = emit_offset();

  // generate code
  assert(emit_offset() % CodeDesc::kAlignment == 0);
  global_trap_handler_ = new NLabel(proc_);  // explicitly deallocated
  return_ = NULL;  // not used by initialization code

  Prologue(NULL, true);

  ChkFunPtr<void (Proc* proc, size_t statics_size)>
      fun_ptr(NSupport::AllocStatics);
  { FunctionCall fc(this, fun_ptr, NULL, NULL, true);
    Operand statics_size_imm(AM_IMM, statics_size);
    PushOperand(&statics_size_imm);
  }
  TrapIfInfo(true);

  Statics* statics = symbol_table->statics();
  for (int i = 0; i < statics->length(); i++)
    Execute(statics->at(i));

  // return TERMINATED to caller to indicate successful initialization
  Operand terminated_status(AM_IMM, Proc::TERMINATED);
  asm_.Load(AM_EAX, &terminated_status);

  // jump to epilogue
  NLabel epilogue(proc_);
  BranchShort(branch, &epilogue);

  // handle initialization failure
  // (only generate this code if needed)
  if (global_trap_handler_->is_linked()) {
    assert(asm_.esp_offset() == 0);
    Bind(global_trap_handler_);
    Operand trap_info(AM_IMM, "initialization failed");
    // return FAILED to caller to indicate initialization failure
    Trap(&trap_info, true, AM_NONE, 0);
  }
  delete global_trap_handler_;
  global_trap_handler_ = NULL;

  Bind(&epilogue);
  Epilogue(NULL, true);

  // make sure emit_offset is aligned for the trap handler stub
  asm_.align_emit_offset();

  // update code range in line info entry
  init_line_info->set_code_range(beg, emit_offset());

  // we must not have any open trap ranges
  assert(current_trap_range_ == NULL);
}


void NCodeGen::GenerateFunction(Statics* statics, Function* fun) {
  CHECK(error_count_ == 0) << ": code generator in error state";
  do_statics_ = false;
  tables_ = NULL;
  function_ = fun;
  asm_.set_esp_offset(0);
  asm_.set_dead_code(false);
  assert(stack_height() == 0);

  // the function entry is only used at runtime to initialize a closure
  // it is therefore not too late to create the entry label here, since all
  // functions are compiled before execution starts
  // the compilation of a function call requires the closure offset,
  // which is allocated before any function is compiled; or the entry itself,
  // which is patched into the call instruction
  if (fun->entry() == NULL)
    fun->set_entry(NCodeGen::NewLabel(proc_));

  // add a line info entry to record the start of the function code
  // do not use node 'fun', because fun->set_code_range() is called when
  // constructing a closure for fun, i.e. fun->code_range() does not
  // point to fun's code
  Empty* fun_line_info = Empty::New(proc_, fun->file_line());
  AddLineInfo(fun_line_info);
  int beg = emit_offset();

  // generate code
  assert(beg % CodeDesc::kAlignment == 0);
  global_trap_handler_ = new NLabel(proc_);  // explicitly deallocated
  return_ = new NLabel(proc_);  // explicitly deallocated

  // set function entry point
  Bind(fun->entry());

  const bool is_main = fun->name() != NULL && strcmp(fun->name(), "$main") == 0;
  Prologue(fun, is_main);

  Execute(fun->body());

  if (fun->ftype()->has_result()) {
    // Missing return.  Create a position for the start of the function.
    // (We do not have a line count and so cannot compute the line number of
    // the end of the function without counting newlines.)
    FileLine* fl = fun->file_line();
    //FileLine* endfl = FileLine::New(proc_, fl->file(), fl->line(),
    //                                fl->offset(), 0);
    szl_string msg;
    if (fun->name() == NULL) {
      msg = proc_->PrintString(
          "missing return in anonymous function that begins at %L", fl);
    } else {
      msg = proc_->PrintString(
          "missing return in function %s, which begins at %L", fun->name(), fl);
    }
    Operand trap_info(AM_IMM, msg);
    Trap(&trap_info, true, AM_NONE, 0);
  }

  assert(asm_.esp_offset() == 0);
  Bind(return_);

  if (is_main) {
    // return TERMINATED to caller to indicate successful execution of $main
    Operand terminated_status(AM_IMM, Proc::TERMINATED);
    asm_.Load(AM_EAX, &terminated_status);
  }

  Epilogue(fun, is_main);

  asm_.set_esp_offset(0);
  // handle undefined results
  // (only generate this code if needed)
  if (global_trap_handler_->is_linked()) {
    assert(asm_.esp_offset() == 0);
    Bind(global_trap_handler_);
    // we return 0 and rely on the caller to check the returned result
    Operand zero(AM_IMM, 0);
    asm_.Load(AM_EAX, &zero);
    Branch(branch, return_);
  }
  delete global_trap_handler_;
  global_trap_handler_ = NULL;
  delete return_;
  return_ = NULL;

  // make sure emit_offset is aligned for the next function
  asm_.align_emit_offset();

  // update code range in line info entry
  fun_line_info->set_code_range(beg, emit_offset());

  // we must not have any open trap ranges
  assert(current_trap_range_ == NULL);
}


// ----------------------------------------------------------------------------
// Debugging

void NCodeGen::AddLineInfo(Node* x) {
  Node* last = line_num_info_->is_empty() ? NULL : line_num_info_->last();
  // at most one record per line, skip additional statements on same line
  if (last == NULL || x->line() != last->line() || x->file() != last->file()) {
    line_num_info_->Append(x);
    if (FLAGS_v > 1)
      F.print("%s:%d\n%1N\n", x->file(), x->line(), x);
  }
}


// ----------------------------------------------------------------------------
// Control flow

NLabel* NCodeGen::NewLabel(Proc* proc) {
  return NEWP(proc, NLabel);
}


void NCodeGen::Bind(Label* L) {
  asm_.set_dead_code(false);  // code following a label target is alive
  down_cast<NLabel*>(L)->bind_to(emit_offset(), asm_.esp_offset(), code_buffer());
}


void NCodeGen::Branch(Opcode op, Label* L) {
  assert(x_.am != AM_NONE || op == branch);
  Branch(op, &x_, L, false);
}


void NCodeGen::BranchShort(Opcode op, Label* L) {
  assert(x_.am != AM_NONE || op == branch);
  Branch(op, &x_, L, true);
}


void NCodeGen::BranchShort(Opcode op, Operand* n, Label* L) {
  Branch(op, n, L, true);
}


void NCodeGen::Branch(Opcode op, Operand* n, Label* L, bool short_branch) {
  int offset;  // branch distance patch offset (will be patched later)
  CondCode cc = CC_NONE;
  if (op == branch) {
      offset = short_branch ? asm_.JmpRel8(0) : asm_.JmpRel32(0);
  } else {
    if (n->am != AM_CC) {
      LoadOperand(n, RS_ANY);
      Operand bool_val;
      Deref(n, &bool_val, BoolVal::val_size(), BoolVal::val_offset());
      LoadOperand(&bool_val, RS_BYTE);
      asm_.TestReg(&bool_val, bool_val.am);
      assert(!is_ref_incrd(&bool_val));
      // otherwise, ReleaseOperand would call dec_ref and lose cc
      ReleaseOperand(&bool_val);
      cc = CC_NE;
    } else {
      cc = n->cc;
      n->Clear();
    }

    switch (op) {
      case branch_true:
        break;

      case branch_false:
        cc = NegateCC(cc);
        break;

      default:
        ShouldNotReachHere();
    }

    if (cc == CC_FALSE)
      return;  // condition always false, do not emit any code
    else if (cc == CC_TRUE)
      offset = short_branch ? asm_.JmpRel8(0) : asm_.JmpRel32(0);
    else
      offset = short_branch ? asm_.JccRel8(cc, 0) : asm_.JccRel32(cc, 0);
  }

  // safe to call the following, even in dead code
  int dist = down_cast<NLabel*>(L)->offset(offset, asm_.esp_offset(), short_branch);
  // dist is patched as zero now if the label's position is not known yet and
  // will be patched later when the label gets bound
  if (short_branch)
    asm_.PatchRel8(offset, dist);
  else
    asm_.PatchRel32(offset, dist);

  // code following an unconditional branch is dead
  if (op == branch || cc == CC_TRUE)
    asm_.set_dead_code(true);
}


// Call trap handler
// If isp is not AM_NONE, an intrinsic returned an error and isp is the register
// containing the interpreter stack pointer prior to the intrinsic call, which
// took num_args arguments; we can therefore reset the stack pointer even if
// the intrinsic left values on the interpreter stack.
void NCodeGen::Trap(const Operand* trap_info, bool fatal, AddrMod isp, int num_args) {
  int saved_esp_offset = asm_.esp_offset();
  // In 64-bit mode, we may have function arguments already loaded in
  // registers with their ref counts incremented; we need to spill
  // live register-allocated arguments to the native stack so that the
  // trap handler can decrement their ref counts.
  if (function_call_ != NULL) {
    const int num_args = function_call_->num_args();
    const int num_reg_args = num_args > kMaxNumRegParams ? kMaxNumRegParams : num_args;
    int arg_pos = function_call_->arg_pos();
    if (arg_pos < num_reg_args) {
      // num_reg_args - arg_pos registers are already loaded
      // note that they all contain Sawzall values, because non-Sawzall values
      // are passed last and do not cause traps
      RegSet live_arg_regs = (all_arg_regset[num_reg_args] -
                              all_arg_regset[arg_pos]) & regs_.live();
      asm_.PushRegs(live_arg_regs);
    }
  }
  NLabel* handler;
  if (trap_info != NULL) {
    handler = fatal ? fatal_trap_handler_with_info_ : trap_handler_with_info_;
#if defined(__i386__)
    assert(isp != AM_EAX);
    asm_.Load(AM_EAX, trap_info);
#elif defined(__x86_64__)
    assert(isp != AM_EDI);
    asm_.Load(AM_EDI, trap_info);
#endif
  } else {
    handler = fatal ? fatal_trap_handler_ : trap_handler_;
  }
  // Before calling a function and starting to push the function's parameters on
  // the stack, the generated code saves caller-saved registers on the stack and
  // pads the stack so that the total stack space used for the call (call area)
  // remains aligned to a 16-byte boundary. See frame.h for call area layout.
  // The member variable esp_offset_ keeps track of the (negative) size of the
  // call area as it grows.
  // The value of esp_offset_ is 0 at the beginning of each Sawzall statement,
  // but may be <= 0 when a trap occurs. A trap is actually a call to the
  // trap handler that can occur while a call area is being built, e.g. when a
  // function parameter to be pushed on the stack is actually undefined.
  // A trap range may start in the middle of a statement and its target may also
  // be in the middle of a statement, where esp_offset_ <= 0.
  // However, esp_offset_ at the beginning of the trap range must match
  // esp_offset_ at the target location. The trap handler must adjust the stack
  // pointer before continuing execution at the target location.
  // The trap handler could calculate the necessary native stack adjustment by
  // calculating the distance between the stack pointer (at the time of the
  // trap) and the frame pointer (minus frame_size). However, the frame size
  // is dependent on the trapped function, which would need to be identified
  // from the trapped pc. For faster trap handling, it is better to pass the
  // stack adjustment known at compiled time to the trap handler.
  // The interpreter stack pointer may also need adjustment, since arguments
  // to intrinsics are passed on the interpreter stack. The adjustment value
  // cannot be derived at trap handling time (because no calling frames are
  // pushed on the interpreter stack in native mode). Therefore, we calculate
  // the adjustment value at compile time and pass it to the trap handler.
  // However, this does not work for intrinsics that return an error and leave
  // values on the stack. In this error case, we pass an absolute stack pointer
  // to the trap handler.
  int target_stack_height = 0;
  int target_native_stack_height = 0;
  if (current_trap_range_ != NULL) {
    target_stack_height = current_trap_range_->stack_height();
    target_native_stack_height = current_trap_range_->native_stack_height();
  }
  // read esp offset before pushing trap arguments
  int native_stack_height = -asm_.esp_offset()/sizeof(Val*);
  Operand native_sp_adjust(AM_IMM, native_stack_height - target_native_stack_height);
  asm_.Push(&native_sp_adjust);
  if (isp != AM_NONE) {
    // pass the absolute stack pointer at the target, rather than an adjustment
    asm_.AddImmReg(isp, (num_args + stack_height_ - target_stack_height)*kPtrSize);
    asm_.PushReg(isp);
    regs_.ReleaseRegs(isp);
  } else {
    Operand sp_adjust(AM_IMM, stack_height_ - target_stack_height);
    asm_.Push(&sp_adjust);
  }
  int offset = asm_.CallRel32(0);
  int rel32 = handler->offset(offset, 0, false);  // stub esp_offset is set to 0
  asm_.PatchRel32(offset, rel32);
  // trap handler will not return here, restore previous esp offset
  asm_.set_esp_offset(saved_esp_offset);
}


// Call trap handler if operand is undef
// pass proc_->trap_info_ to trap handler if pass_info is true
void NCodeGen::TrapIfUndefOperand(Operand* n, bool pass_info) {
  assert(IsIntReg(n->am));
  asm_.TestReg(n, n->am);
  Operand null_ptr(AM_CC, CC_E);
  NLabel not_null(proc_);
  BranchShort(branch_false, &null_ptr, &not_null);
  if (pass_info) {
    Operand proc_ptr(AM_BASED + AM_EBP, kPtrSize, kProcPtrOffset);
    LoadOperand(&proc_ptr, RS_ANY);
    Operand trap_info(AM_BASED + proc_ptr.am, kPtrSize, Proc::trap_info_offset());
    ReserveRegs(&trap_info);
    ReleaseOperand(&proc_ptr);
    Trap(&trap_info, false, AM_NONE, 0);
    ReleaseOperand(&trap_info);
  } else {
    Trap(NULL, false, AM_NONE, 0);
    // remember this trap site and the variable that was loaded
    // if n->var is NULL, it was a function result
    assert(current_trap_range_ != NULL);
    if (n->var != NULL)
      n->var->UsesTrapinfoIndex(proc_);  // make sure we have a slot for it
    current_trap_range_->AddTrap(emit_offset() - 1, n->var);
  }
  Bind(&not_null);
  clear_flags(n, kCheckUndef | kCheckNull);
}


// Call trap handler if trap_info operand is not null
void NCodeGen::TrapIfOperandNotNull(Operand* trap_info, bool fatal, AddrMod isp, int num_args) {
  assert(IsIntReg(trap_info->am));
  asm_.TestReg(trap_info, trap_info->am);
  Operand null_trap_info(AM_CC, CC_E);
  NLabel no_error(proc_);
  BranchShort(branch_true, &null_trap_info, &no_error);
  // No need to assign result to proc->trap_info_ here, since it will be
  // assigned in NSupport::HandleTrap
  Trap(trap_info, fatal, isp, num_args);
  ReleaseOperand(trap_info);
  Bind(&no_error);
#ifdef NDEBUG
  // perform this optimization in optimized mode only
  if (x_.am == AM_EAX)
    // the result of the intrinsic does not need to be tested, since
    // proc_->trap_info_ was just tested and intrinsic should not return an
    // undef Val without setting proc_->trap_info_
    clear_flags(&x_, kCheckUndef | kCheckNull);
#else
  // check result for undef and null in debug mode (do not clear flags)
#endif
}


// Call trap handler if proc_->trap_info_ is non-null
void NCodeGen::TrapIfInfo(bool fatal) {
  Operand proc_ptr(AM_BASED + AM_EBP, kPtrSize, kProcPtrOffset);
  LoadOperand(&proc_ptr, RS_ANY);
  Operand trap_info(AM_BASED + proc_ptr.am, kPtrSize, Proc::trap_info_offset());
  ReserveRegs(&trap_info);
  ReleaseRegs(&proc_ptr);
  LoadOperand(&trap_info, RS_ANY);
  TrapIfOperandNotNull(&trap_info, fatal, AM_NONE, 0);
}


// Save live caller-saved registers and align the stack in preparation of a
// function call that takes num_args parameters of size kPtrSize each.
// Push header size for stack traversal by trap handler. See frame.h.
// Note that we generate code in a single pass; branch offsets may not be
// generated immediately, but their size is fixed, because we do not shift code
// around. Similarly, we have to decide which registers need to be saved across
// calls before pushing arguments on the stack. We pass the register ref counts
// used by the arguments (arg_regs parameter) and calculate which registers will
// still be alive after the argument registers have been freed. Out of these,
// we save the caller-saved ones and assign their state to saved_regs.
// Align the stack and return the stack adjustment to perform after the call.
size_t NCodeGen::ReserveCallArea(int num_args, const RegsState* arg_regs, RegsState* saved_regs) {
  *saved_regs = regs_;
  saved_regs->ReleaseRegs(arg_regs);
  saved_regs->ReleaseRegs(~RS_CALLER_SAVED & RS_ANY);  // keep only caller-saved
  asm_.PushRegs(saved_regs->live());
  regs_.ReleaseRegs(saved_regs);  // saved regs are not live anymore

  // do not reserve stack space in call area for arguments passed in registers
  const int num_reg_args = num_args > kMaxNumRegParams ? kMaxNumRegParams : num_args;
  const int num_mem_args = num_args - num_reg_args;
  size_t args_size = num_mem_args*kPtrSize;

  // align call area size by inserting padding
  int call_area_size = -asm_.esp_offset() + kPtrSize + args_size;
  int padding = Align(call_area_size, NFrame::kStackAlignment) - call_area_size;
  if (padding == NFrame::kStackAlignment - kPtrSize)
    // padding is only necessary because of the pushed header size, suppress
    return args_size;  // stack adjustment after call without padding

  if (padding != 0)
    asm_.AddImm8Esp(-padding);

  // The trap handler needs to see the saved registers, which are always valid
  // Val*, so that it can decrement their ref count when resetting the stack.
  // Do not include them in the header skipped over by the trap handler:
  int header_size = /*NumRegs(saved_regs->live())*kPtrSize +*/ padding;
  assert(0 <= header_size && header_size <= NFrame::kMaxCallAreaHeaderSize);
  Operand header_size_imm(AM_IMM, header_size);
  asm_.Push(&header_size_imm);

  return args_size + kPtrSize + padding;  // stack adjustment after call
}


void NCodeGen::ReleaseCallArea(size_t esp_adjust, const RegsState* saved_regs) {
  if (esp_adjust)
    asm_.AddImmReg(AM_ESP, esp_adjust);

  if ((saved_regs->live() & RS_EAX) != RS_EMPTY && x_.am == AM_EAX) {
    // eax was saved before the call and a result is returned in eax
    // move result to a callee-saved register before popping eax
    // do not perform undef checks yet, since the stack is not at the correct
    // level for trap handling (saved regs must be popped first)
    ReleaseRegs(&x_);
    AddrMod reg = GetReg(RS_CALLEE_SAVED);
    asm_.Load(reg, &x_);
    x_.am = reg;
  }
  asm_.PopRegs(saved_regs->live());
  regs_.ReserveRegs(saved_regs);  // restored regs are live again
}


// Call function pointed to by fun_ptr
void NCodeGen::CallFunPtr(Operand* fun_ptr, int num_args, bool has_vargs) {
  assert(fun_ptr->am != AM_IMM || fun_ptr->value != 0);
#if defined(__x86_64__)
  Operand zero(AM_IMM, 0);
  zero.size = sizeof(int);  // no need to set RAX; actually, only 8-bit are used
  if (has_vargs)  // EAX contains the number of float registers in the va_list
    LoadOperand(&zero, RS_EAX);
#endif
  LoadOperand(fun_ptr, RS_TMP);
  asm_.CallIndir(fun_ptr);
  ReleaseOperand(fun_ptr);
#if defined(__x86_64__)
  ReleaseOperand(&zero);
#endif
  const int num_reg_args = num_args > kMaxNumRegParams ? kMaxNumRegParams : num_args;
  assert((regs_.live() & RS_CALLER_SAVED) == all_arg_regset[num_reg_args]);
  regs_.ReleaseRegs(all_arg_regset[num_reg_args]);
  assert(x_.am == AM_NONE);
}


// Call Sawzall function, possibly via closure
void NCodeGen::CallSzlFun(Expr* fun, int num_args) {
  Function* f = fun->AsFunction();
  if (f != NULL) {
    // function literal, use its level and address directly
    PushBP(f->context_level());  // static link
    if (f->entry() == NULL)
       f->set_entry(NCodeGen::NewLabel(proc_));

    int offset = asm_.CallRel32(0);
    // note that esp_offset at function entry is zero
    int dist = down_cast<NLabel*>(f->entry())->offset(offset, 0, false);
    asm_.PatchRel32(offset, dist);

    const int num_reg_args = num_args > kMaxNumRegParams ? kMaxNumRegParams : num_args;
    assert((regs_.live() & RS_CALLER_SAVED) == all_arg_regset[num_reg_args]);
    regs_.ReleaseRegs(all_arg_regset[num_reg_args]);
    assert(x_.am == AM_NONE);

  } else {
    Load(fun, false);
    // todo can we suppress undef and null checks here? can closure be null?
    LoadOperand(&x_, RS_ANY & ~arg_regset[0]);  // do not lock arg reg used to pass static link

    Operand static_link(AM_BASED + x_.am, ClosureVal::context_size(), ClosureVal::context_offset());
    ReserveRegs(&static_link);
    PushOperand(&static_link);

    Operand fun_ptr(AM_BASED + x_.am, ClosureVal::entry_size(), ClosureVal::entry_offset());
    ReserveRegs(&fun_ptr);
    ReleaseOperand(&x_);
    CallFunPtr(&fun_ptr, num_args, false);
  }
}


// Setup result operand according to result_type
void NCodeGen::SetupFunctionResult(Type* result_type, bool check_err) {
  if (result_type != NULL && !result_type->is_void()) {
    assert(result_type->size() == kPtrSize);
    Operand result(GetReg(RS_EAX));
    set_type(&result, result_type);
    if (check_err)
      set_flags(&result, kCheckUndef | kCheckNull);
    if (result.flags & kIsSzlVal)
      set_flags(&result, kRefIncrd);

    x_ = result;
  }
}


// Assign arg_regs with the set of registers used by the operands x and y that
// will be passed as function arguments. These arg_regs registers will not be
// live across the call and therefore do not need to be saved in ReserveCallArea.
// In 64-bit mode, we make sure that x and y arguments can be assigned to their
// final argument passing registers without causing a register allocation
// conflict. The argument passing sequence is as follows:
//   R[n-1] = arg[n-1];  with n = number of arguments passed in registers
//   ...
//   R[ypos] = y;
//   ...
//   R[xpos] = x;
//   ...
//   R[0] = arg[0];
// A register conflict exists if x uses any register in the range  R[xpos+1]..
// R[n-1] or if y uses any register in the range R[ypos+1]..R[n-1].
// If such a conflict is detected, we preload the argument to free the registers
// before starting the sequence above. We are careful not to create a conflict
// when preloading an argument.
void NCodeGen::PreloadArgs(Operand* x, int xpos, Operand* y, int ypos,
                           int num_reg_args, RegsState* arg_regs) {
#if defined(__x86_64__)
  assert(xpos < num_reg_args && num_reg_args <= kMaxNumRegParams);
  RegSet x_conflict = all_arg_regset[num_reg_args] - all_arg_regset[xpos + 1];
  bool preload_x = (Regs(x->am) & x_conflict) != RS_EMPTY;
  bool preload_y = false;
  RegSet y_conflict = RS_EMPTY;
  if (y != NULL) {
    assert(xpos < ypos && ypos < num_reg_args && ypos != xpos);
    RegSet y_conflict = all_arg_regset[num_reg_args] - all_arg_regset[ypos + 1];
    preload_y = (Regs(y->am) & y_conflict) != RS_EMPTY;
  }
  while (preload_x || preload_y) {
    if (preload_x) {
      if ((regs_.live() & arg_regset[xpos]) == RS_EMPTY) {
        // load x to its final arg reg since it is available
        LoadOperand(x, arg_regset[xpos]);
        preload_x = false;
      } else if ((regs_.live() & (RS_ANY - x_conflict)) == RS_EMPTY) {
        // load x to any non-conflicting available register
        LoadOperand(x, RS_ANY - x_conflict);
        preload_x = false;
      } else {
        // loading y should free up a register to load x
        if (!preload_y) {
          // we are out of registers, the following load will give an error
          LoadOperand(x, arg_regset[xpos]);
          preload_x = false;
        }
      }
    }
    if (preload_y) {
      if ((regs_.live() & arg_regset[ypos]) == RS_EMPTY) {
        // load y to its final arg reg since it is available
        LoadOperand(y, arg_regset[ypos]);
        preload_y = false;
      } else if ((regs_.live() & (RS_ANY - y_conflict)) == RS_EMPTY) {
        // load y to any non-conflicting available register
        LoadOperand(y, RS_ANY - y_conflict);
        preload_y = false;
      } else {
        // we are out of registers, the following load will give an error
        LoadOperand(y, arg_regset[ypos]);
        preload_y = false;
      }
    }
  }
  assert((Regs(x->am) & x_conflict) == RS_EMPTY);
  if (y != NULL)
    assert((Regs(y->am) & y_conflict) == RS_EMPTY);
#endif
  arg_regs->ReserveRegs(x->am);
  if (y != NULL)
    arg_regs->ReserveRegs(y->am);
}


void NCodeGen::PreloadArg(Operand* x, int pos, int num_reg_args, RegsState* arg_regs) {
  PreloadArgs(x, pos, NULL, -1, num_reg_args, arg_regs);
}


void NCodeGen::EmitCounter(Node* x) {
  if (FLAGS_szl_bb_count) {
    int n = line_num_info_->length();
    Trace(&tlevel_, "count %d offset %d line %d", n, x->file_line()->offset(),
          x->file_line()->line());
    line_num_info_->Append(x);
    if (FLAGS_v > 1)
      F.print("%s:%d\n%1N\n", x->file(), x->line(), x);
    // we emit a call to the native support counting routine IncCounter
    ChkFunPtr<void (Proc*, int)> fun_ptr(NSupport::IncCounter);
    FunctionCall fc(this, fun_ptr, NULL, NULL, false);
    Operand counter(AM_IMM, n);
    PushOperand(&counter);
  }
}


// ----------------------------------------------------------------------------
// Expression code

void NCodeGen::Visit(Node* x) {
  int beg = emit_offset();
  if (x->line_counter())
    EmitCounter(x);
  Statement* enclosing_statement = statement_;
  if (x->AsStatement() != NULL) {
    // current innermost statement node
    statement_ = x->AsStatement();
    // record line number info unless x is a TypeDecl or a VarDecl that is either
    // 1) not processed in this traversal or 2) for which no code is generated
    if (x->AsTypeDecl() == NULL) {
      VarDecl* vd = x->AsVarDecl();
      if (vd == NULL || (vd->is_static() == do_statics() &&
          (vd->type()->is_output() || vd->init() != NULL)))
        AddLineInfo(x);
    }
  }
  if (expr_ == NULL && x->AsExpr() != NULL) {
    // current top level expression node
    expr_ = x->AsExpr();
    x->Visit(this);
    expr_ = NULL;
  } else {
    x->Visit(this);
  }
  // Only set node's code range if basic block counting was requested for this
  // node or if some code was generated while visiting this node; this prevents
  // the code range of a static VarDecl node to be overwritten during the second
  // visit of the VarDecl node (bug #1601218).
  int end = emit_offset();
  if (end > beg || x->line_counter())
    x->set_code_range(beg, end);
  statement_ = enclosing_statement;
}


AddrMod NCodeGen::GetReg(RegSet rs) {
#if defined(__x86_64__)
  // if we are currently passing arguments to a function, try to allocate the
  // next argument register in order to save a register move later
  if (function_call_ != NULL) {
    int next_arg_pos = function_call_->arg_pos() - 1;
    if (next_arg_pos < kMaxNumRegParams) {
      // operand is not pushed on the stack, but passed in a register
      RegSet arg_rs = arg_regset[next_arg_pos] & rs;
      if (arg_rs != RS_EMPTY) {
        AddrMod reg = regs_.GetReg(arg_rs);
        if (reg != AM_NONE)
          return reg;
      }
    }
  }
#endif
  AddrMod reg = regs_.GetReg(rs);  // get the first available rs register
  if (reg == AM_NONE) {  // all registers of rs are busy
    // we cannot spill a register here, since we don't have enough info
    Node* x;
    if (expr_ != NULL)
      x = expr_;
    else
      x = statement_;
    if (x != NULL) {
      Error(proc_->PrintString(
        "%L: native compiler error: no free registers - simplify %n",
        x->file_line(), source(), x));
    } else {
      Error("native compiler error: no free registers - simplify expression");
    }
    // return a register of rs to avoid further errors
    reg = FirstReg(rs);
    regs_.ReserveRegs(reg);
  }
  return reg;
}


// Loads bp corresonding to 'level' into one of 'rs' registers
AddrMod NCodeGen::GetBP(int level, RegSet rs) {
  int delta = bp_delta(level);
  assert(delta >= 0);
  // nothing to do if delta == 0 (same scope, use ebp)
  // note that level() is set to 1 when compiling init natively, because
  // locals of init at level 0 are allocated on the interpreter stack
  if (delta == 0) {
    return AM_EBP;
  } else {
    Operand static_link(AM_BASED + AM_EBP, kPtrSize, kStaticLinkOffset);
    LoadOperand(&static_link, rs);
    AddrMod bp_reg = static_link.am;
    while (--delta > 0) {
      Operand bp(AM_BASED + bp_reg, kPtrSize, kStaticLinkOffset);
      asm_.Load(bp_reg, &bp);
    }
    return bp_reg;
  }
}


// Allocate a register of rs and load the address of proc->state_.sp_
AddrMod NCodeGen::GetISPAddr(RegSet rs) {
  Operand proc_ptr(AM_BASED + AM_EBP, kPtrSize, kProcPtrOffset);
  LoadOperand(&proc_ptr, rs);
  Operand sp_addr(AM_BASED + proc_ptr.am, kPtrSize, Proc::state_sp_offset());
  LoadOperandEA(&sp_addr, rs);
  return sp_addr.am;
}


// Push the address of proc->state_.sp_ on the native stack
// Allocate a callee-saved reg and load proc->state_.sp_
void NCodeGen::PushISPAddr(AddrMod* isp) {
  Operand sp_addr(GetISPAddr(RS_ANY));
  assert(IsIntReg(sp_addr.am));
  Operand sp(AM_INDIR + sp_addr.am, kPtrSize, 0);
  ReserveRegs(&sp);
  LoadOperand(&sp, RS_CALLEE_SAVED);  // sp = *(&proc->state_.sp_)
  *isp = sp.am;
  PushOperand(&sp_addr);
}


void NCodeGen::ReserveRegs(const Operand* n) {
  regs_.ReserveRegs(n->am);
}


void NCodeGen::ReleaseRegs(const Operand* n) {
  regs_.ReleaseRegs(n->am);
}

// Load an operand for x into x_
// to be pushed later by the code that calls this function.
void NCodeGen::Load(Expr* x, bool is_lhs) {
  NLabel ttarget(proc_);
  NLabel ftarget(proc_);
  LoadConditional(x, is_lhs, &ttarget, &ftarget);
  if (x_.am == AM_CC) {
    // ttarget and/or ftarget may already be linked here
    assert(x->type()->is_bool());
    Branch(branch_false, &ftarget);
    assert(x_.am == AM_NONE);
    // Branch(branch, &ttarget); would always work, but can be optimized away:
    if (ttarget.is_linked()) {
      ;  // no need to branch to ttarget, since ttarget is already linked and
         // will be bound to current emit position to load true
    } else {
      // do not link ttarget in order to save some code below, load true instead
      Operand true_imm(AM_IMM, Factory::NewBool(proc_, true));
      set_type(&true_imm, x->type());
      x_ = true_imm;
    }
  }
  if (ttarget.is_linked() || ftarget.is_linked()) {
    assert(x->type()->is_bool());
    // we have at least one conditional value
    // that has been "translated" into a branch,
    // thus it needs to be loaded explicitly again
    Operand loaded_bool;  // we need a common reg for all paths
    NLabel loaded(proc_);
    if (x_.am != AM_NONE) {
      // don't lose current bool
      LoadOperand(&x_, RS_ANY);
      loaded_bool = x_;
      x_.Clear();
      Branch(branch, &loaded);
    } else {
      loaded_bool.am = GetReg(RS_ANY);
    }
    assert(IsIntReg(loaded_bool.am));
    bool both = ttarget.is_linked() && ftarget.is_linked();
    // reincarnate "true", if necessary
    if (ttarget.is_linked()) {
      Bind(&ttarget);
      Operand true_imm(AM_IMM, Factory::NewBool(proc_, true));
      asm_.Load(loaded_bool.am, &true_imm);  // use same register
    }
    // if both "true" and "false" need to be reincarnated,
    // jump across code for "false"
    if (both)
      Branch(branch, &loaded);
    // reincarnate "false", if necessary
    if (ftarget.is_linked()) {
      Bind(&ftarget);
      Operand false_imm(AM_IMM, Factory::NewBool(proc_, false));
      asm_.Load(loaded_bool.am, &false_imm);
    }
    // everything is loaded at this point
    Bind(&loaded);
    set_type(&loaded_bool, x->type());
    x_ = loaded_bool;
  }
}


// Make sure the operand n is not deleted by side effects or leaked while
// loading the expression x in operand nx
void NCodeGen::ProtectAndLoad(Operand* n, Expr* x, bool is_lhs, Operand* nx) {
  // We need to increment the ref count of the operand n if evaluating
  // the expression x could either call a function touching the operand n via
  // side effects and possibly deleting the operand n prematurely, or try to
  // allocate some memory resulting in a call to the garbage collector.
  // This cannot be the case if the operand n is an immediate value.
  if (n->am != AM_NONE && n->am != AM_IMM) {
    assert(is_szl_val(n));
    bool x_can_call = x->CanCall(is_lhs);
    if (is_ref_incrd(n)) {
      // If n's ref count is off by one, a trap occuring while evaluating x
      // would cause n to leak, so correct n's ref count before evaluating x.
      // This applies whether x calls a function or not.
      // However, if n is going to be saved and its ref count is going to be
      // incremented below, we can optimize away both the dec ref and inc ref
      if (!x_can_call) {
        clear_flags(n, kRefIncrd);
        DecRefOperand(n, RS_ANY);
      }
    }
    if (x_can_call) {
      IncRefOperand(n, RS_ANY);
      LoadOperand(n, RS_ANY);
      // We push the protected operand on the native stack so that, in case of a
      // trap, its reference count is decremented by the trap handler.
      asm_.PushReg(n->am);
      ReleaseRegs(n);
      Load(x, is_lhs);
      *nx = x_;
      x_.Clear();
      n->am = GetReg(RS_ANY);
      asm_.PopReg(n->am);
      // Instead of calling DecRefOperand(n, RS_ANY), we set the kRefIncrd flag
      assert(!is_ref_incrd(n));
      set_flags(n, kRefIncrd);
      return;
    }
  }
  // no need to protect n
  Load(x, is_lhs);
  *nx = x_;
  x_.Clear();
}


// Push Val* operand onto native stack and inc ref count
void NCodeGen::PushVal(Operand* n) {
  IncRefOperand(n, RS_ANY);
  PushOperand(n);
}


// Push Val* operand onto interpreter stack and inc ref count
void NCodeGen::IPushVal(Operand* n) {
  IncRefOperand(n, RS_ANY);
  IPushOperand(n);
}


// Pop Val* operand from top of interpreter stack into x_ after checking that
// no error was returned by the intrinsic; the register isp contains the
// interpreter stack pointer as it was passed to the intrinsic and num_args
// indicates the number of args passed to the intrinsic.
void NCodeGen::IPopVal(Type* type, AddrMod isp, int num_args, bool check_err) {
  // Call trap handler if have char* result and it is non-null
  if (check_err)
    TrapIfOperandNotNull(&x_, false, isp, num_args);
  else
    regs_.ReleaseRegs(isp);

  assert(x_.am == AM_NONE);
  if (type == NULL || type->is_void())
    return;

  assert(type->size() == kPtrSize);
  x_.size = kPtrSize;
  set_type(&x_, type);
  set_flags(&x_, kRefIncrd);  // was inc ref'd when pushed
  if (check_err)
    set_flags(&x_, kCheckUndef | kCheckNull);
  Operand sp_addr(GetISPAddr(RS_ANY));
  x_.am = GetReg(RS_ANY);
  assert(IsIntReg(sp_addr.am));
  Operand sp(AM_INDIR + sp_addr.am, kPtrSize, 0);
  ReserveRegs(&sp);
  LoadOperand(&sp, RS_ANY);  // sp = *(&proc->state_.sp_)
  Operand val(AM_INDIR + sp.am, kPtrSize, 0);
  asm_.Load(x_.am, &val);  // x_ = *sp
  asm_.AddImmReg(sp.am, kPtrSize);  // sp++
  // Do not decrement stack_height_ here, since it was not adjusted for
  // intrinsic result; stack_height_ only counts pushed arguments to intrinsics
  Operand new_sp(AM_INDIR + sp_addr.am, kPtrSize, 0);
  asm_.Store(&new_sp, sp.am);  // *(&proc->state_.sp_) = sp
  ReleaseRegs(&sp);
  ReleaseRegs(&sp_addr);
}


// Push expr onto native stack and inc ref count
void NCodeGen::PushExpr(Expr* x, bool is_lhs) {
  Load(x, is_lhs);
  PushVal(&x_);
}


// Push expr onto interpreter stack and inc ref count
void NCodeGen::IPushExpr(Expr* x) {
  Load(x, false);
  IPushVal(&x_);
}


// Push num_args expr of a list args starting at from_arg onto native stack
void NCodeGen::PushExprs(const List<Expr*>* args, int from_arg, int num_args) {
  for (int i = 0; i < num_args; ++i)
    PushExpr(args->at(i + from_arg), false);
}


// Push num_args expr of a list args in reverse order onto native stack
void NCodeGen::PushReverseExprs(const List<Expr*>* args, int num_args) {
  for (int i = num_args; --i >= 0; )
    PushExpr(args->at(i), false);
}

// Push num_args expr of a Composite args in reverse order onto native stack
void NCodeGen::PushReverseExprs(Composite* args, int num_args) {
  for (int i = num_args; --i >= 0; )
    PushExpr(args->at(i), false);
}

// Push num_args expr (from_arg to from_arg + num_args - 1) of a
// Composite args in reverse order onto native stack
void NCodeGen::PushReverseExprs(Composite* args, int from_arg, int num_args) {
  for (int i = num_args; --i >= 0; )
    PushExpr(args->at(from_arg + i), false);
}

// Push base pointer (static link) onto native stack or pass in reg
void NCodeGen::PushBP(int level) {
  Operand bp(GetBP(level, RS_ANY));
  PushOperand(&bp);
}


// Push proc pointer onto native stack
void NCodeGen::PushProc() {
  Operand proc_ptr(AM_BASED + AM_EBP, kPtrSize, kProcPtrOffset);
  assert(!needs_undef_check(&proc_ptr));
  PushOperand(&proc_ptr);
}


// Pushes operand's effective address onto stack
void NCodeGen::PushAddr(Operand* n) {
  // PushAddr does not manage ref count
  assert(!is_szl_val(n) || !is_ref_incrd(n));
  // we ignore the value, we need the address only
  clear_flags(n, kCheckUndef | kCheckNull);
  LoadOperandEA(n, RS_ANY);
  PushOperand(n);
}


// Push num_args expr of a list args in reverse order onto interpreter stack
void NCodeGen::IPushReverseExprs(const List<Expr*>* args, int num_args) {
  for (int i = num_args; --i >= 0; )
    IPushExpr(args->at(i));
}


// Store Val* operand val in dst and manage ref counts
void NCodeGen::StoreVal(Operand* dst, Operand* n, bool check_old_val) {
  IncRefOperand(n, RS_ANY);  // new value
  // IncRefOperand does nothing if kRefIncrd flag is set (e.g.  function result)
  LoadOperand(n, RS_ANY);  // load new value in reg if not already loaded above
  // Make sure an undef n is trapped before old_val's ref count is decremented,
  // otherwise trap handler would decrement it a second time before overwriting
  // old_val with n in dst; LoadOperand performs the undef check if necessary.
  if (check_old_val) {
    Operand old_val = *dst;
    ReserveRegs(&old_val);
    assert(is_szl_val(&old_val));
    clear_flags(&old_val, kCheckUndef);  // can be undef, but no check needed
    set_flags(&old_val, kCheckNull);  // still need to check for NULL
    DecRefOperand(&old_val, RS_ANY);
    ReleaseOperand(&old_val);
  }
  asm_.Store(dst, n->am);
  ReleaseOperand(dst);
  ReleaseOperand(n);
}


// Increment Val* operand dst by delta
void NCodeGen::IncVal(Operand* dst, int delta) {
  assert(x_.am == AM_NONE);
  assert(delta == -1 || delta == 1);

  // fast case falls back to slow case if undef; slow case checks for undef
  clear_flags(dst, kCheckUndef | kCheckNull);

  // attempt the fast case with 2 smi, falling back to slow case if needed
  Operand val = *dst;
  ReserveRegs(&val);
  clear_flags(&val, kCheckUndef);
  LoadOperand(&val, RS_ANY & ~Regs(dst->am));

  NLabel slow_case(proc_);
  NLabel fast_case(proc_);
  NLabel done(proc_);

  // branch to slow case if dst is not an smi
  asm_.TestImm(&val, TaggedInts::tag_mask);
  Operand no_smi(AM_CC, CC_E);
  BranchShort(branch_true, &no_smi, &slow_case);

  // add delta as smi to register
  asm_.AddImmReg(val.am, delta << TaggedInts::ntag_bits);  // sets CC

  // we are done unless there is an overflow
  Operand overflow(AM_CC, CC_O);
  BranchShort(branch_false, &overflow, &fast_case);

  Bind(&slow_case);
  // clone dst operand to finish fast case below, do not reserve registers
  Operand dst_clone = *dst;
  ReleaseRegs(&val);  // forget incremented and overflowed val, free register
  RegsState arg_regs;
  PreloadArg(dst, 1, 2, &arg_regs);
  ChkFunPtr<int (Proc* proc, Val*& var)>
      fun_ptr(delta == -1 ? NSupport::Dec : NSupport::Inc);
  { FunctionCall fc(this, fun_ptr, &arg_regs, SymbolTable::bad_type(), true);
    PushAddr(dst);
  }
  // Inc and Dec return 0 if operand was undef
  if (needs_check(dst))  // do not check x_; FunctionCall sets to check
    TrapIfUndefOperand(&x_, false);
  ReleaseOperand(&x_);
  BranchShort(branch, NULL, &done);

  // finish fast case by storing incremented val into dst
  Bind(&fast_case);
  asm_.Store(&dst_clone, val.am);  // registers were not reserved

  Bind(&done);
}


// Make a unique copy of Val* operand n, return it in x_
void NCodeGen::UniqVal(Operand* n, Type* type) {
  bool check_err = needs_check(n);
  VarDecl* var = n->var;  // for tracking undefined vars
  RegsState arg_regs;
  PreloadArg(n, 1, 2, &arg_regs);
  ChkFunPtr<Val* (Proc* proc, Val*& var)>
      fun_ptr(check_err ? NSupport::CheckAndUniq : NSupport::Uniq);
  { FunctionCall fc(this, fun_ptr, &arg_regs, type, check_err);
    PushAddr(n);
  }
  assert(is_ref_incrd(&x_));
  clear_flags(&x_, kRefIncrd);  // the ref count must stay at one
  set_var(&x_, var);
}


// Loads the operand into a register of the given register set
// Checks if operand is defined if necessary
void NCodeGen::LoadOperand(Operand* n, RegSet rs) {
  assert(n->am != AM_NONE);
  if (!IsIntReg(n->am) || (Regs(n->am) & rs) == RS_EMPTY) {
    ReleaseRegs(n);
    AddrMod reg = GetReg(rs);
    asm_.Load(reg, n);
    n->am = reg;
  }
  if (needs_undef_check(n))
    TrapIfUndefOperand(n, false);
}


// Loads the address of the operand into a register of the given register set
void NCodeGen::LoadOperandEA(Operand* n, RegSet rs) {
  assert(n->am != AM_NONE && !IsIntReg(n->am) && !needs_undef_check(n));
  ReleaseRegs(n);
  AddrMod reg = GetReg(rs);
  asm_.LoadEA(reg, n);
  n->am = reg;
}


// Push operand on top of native stack
void NCodeGen::PushOperand(Operand* n) {
  int arg_pos = function_call_->NextArgPos();
  if (arg_pos < kMaxNumRegParams) {
    // operand is not pushed on the stack, but passed in a register
    LoadOperand(n, arg_regset[arg_pos]);
    // do not release operand here; arg regs are released in CallFunPtr
    // however, operand is not used anymore; set to AM_NONE
    n->Clear();
  } else {
    if (needs_undef_check(n))  // a function result is passed as argument
      TrapIfUndefOperand(n, false);

    asm_.Push(n);
    ReleaseOperand(n);
  }
}


// Push operand on top of interpreter stack
void NCodeGen::IPushOperand(Operand* n) {
  if (needs_undef_check(n))  // a function result is passed as argument
    TrapIfUndefOperand(n, false);

  Operand sp_addr(GetISPAddr(RS_ANY));
  Operand sp(AM_INDIR + sp_addr.am, kPtrSize, 0);
  ReserveRegs(&sp);
  LoadOperand(&sp, RS_ANY);  // sp = *(&proc->state_.sp_)
  asm_.AddImmReg(sp.am, -kPtrSize);  // --sp
  stack_height_++;
  LoadOperand(n, RS_ANY);  // operand may not be loaded yet (e.g. immediate value)
  Operand val(AM_INDIR + sp.am, kPtrSize, 0);
  asm_.Store(&val, n->am);  // *sp = n
  Operand new_sp(AM_INDIR + sp_addr.am, kPtrSize, 0);
  asm_.Store(&new_sp, sp.am);  // *(&proc->state_.sp_) = sp
  ReleaseRegs(&sp);
  ReleaseRegs(&sp_addr);
  ReleaseOperand(n);
}


// Free the resources used by the operand
void NCodeGen::ReleaseOperand(Operand* n) {
  if (is_szl_val(n)) {
    // the operand is a Val*
    if (is_ref_incrd(n)) {
      // its ref_count is off by one, call dec_ref (may also do undef check)
      clear_flags(n, kRefIncrd);
      DecRefOperand(n, RS_ANY);
    } else if (needs_undef_check(n)) {
      // its ref_count is OK but we still have to do an undef check
      LoadOperand(n, RS_ANY);
    }
  }
  ReleaseRegs(n);
  n->Clear();
}


// Increment operand's ref count, unless already incremented
// Note that operand is not guaranteed to be loaded in a reg of rs
void NCodeGen::IncRefOperand(Operand* n, RegSet rs) {
  assert(is_szl_val(n));
  // inc ref count, unless already incremented earlier
  if (is_ref_incrd(n)) {
    clear_flags(n, kRefIncrd);
    // n must have been loaded to increment its count, unless it's a constant
    assert(IsIntReg(n->am) || n->am == AM_IMM);
    return;
  }
  if (is_smi_val(n))
    // smi are not ref counted
    return;

  // note that we do not set kRefIncrd in n->flags here, otherwise the count
  // would get decremented when the operand is released
  // test val, TaggedInts::tag_mask
  // bne skip
  // test val, val
  // be skip
  // inc val->ref_
  // skip:
  if (n->am == AM_IMM) {
    // n->value is the Val pointer value, optimize
    assert((n->value & TaggedInts::tag_mask) == TaggedInts::ptr_tag);  // cannot be an smi, see above
    if (n->value != 0) {
#if defined(__x86_64__)
      if (!IsDWordRange(n->value + Val::ref_offset())) {
        // do not use AM_ABS in 64-bit mode when offset does not fit in 32 bits
        LoadOperand(n, rs);
        Operand ref_count(AM_BASED + n->am, Val::ref_size(), Val::ref_offset());
        asm_.Inc(&ref_count);
        return;
      }
#endif
      Operand ref_count(AM_ABS, Val::ref_size(), n->value + Val::ref_offset());
      asm_.Inc(&ref_count);
    }
  } else {
    LoadOperand(n, rs);
    NLabel skip(proc_);
    if (is_int_val(n)) {
      // if !val->is_ptr() then skip
      asm_.TestImm(n, TaggedInts::tag_mask);
      Operand not_a_ptr(AM_CC, CC_NE);
      BranchShort(branch_true, &not_a_ptr, &skip);
    }
    if (needs_null_check(n)) {
      // if val->is_null() then skip
      asm_.TestReg(n, n->am);
      Operand null_ptr(AM_CC, CC_E);
      BranchShort(branch_true, &null_ptr, &skip);
    }
    Operand ref_count(AM_BASED + n->am, Val::ref_size(), Val::ref_offset());
    asm_.Inc(&ref_count);
    Bind(&skip);
  }
}


// Load operand n and decrement its ref count
void NCodeGen::DecRefOperand(Operand* n, RegSet rs) {
  assert(is_szl_val(n));
  assert(!is_ref_incrd(n));  // flag should have been cleared
  if (is_smi_val(n))
    // smi are not ref counted
    return;

  // no need to optimize for n->am == AM_IMM as in IncRefOperand, because we
  // never call DecRefOperand for a compile-time constant
  LoadOperand(n, rs);
  // test val, TaggedInts::tag_mask
  // bne skip
  // test val, val
  // be skip
  // dec val->ref_
  // skip:
  NLabel skip(proc_);
  if (is_int_val(n)) {
    // if !val->is_ptr() then skip
    asm_.TestImm(n, TaggedInts::tag_mask);
    Operand not_a_ptr(AM_CC, CC_NE);
    BranchShort(branch_true, &not_a_ptr, &skip);
  }
  if (needs_null_check(n)) {
    // if val->is_null() then skip
    asm_.TestReg(n, n->am);
    Operand null_ptr(AM_CC, CC_E);
    BranchShort(branch_true, &null_ptr, &skip);
  }
  Operand ref_count(AM_BASED + n->am, Val::ref_size(), Val::ref_offset());
  asm_.Dec(&ref_count);
  Bind(&skip);
}


// Loads a value in x_. If it is a boolean value, the result may have been
// (partially) translated into branches, or it may have set the condition code
// register. If the condition code register was set, x_.am == AM_CC.
void NCodeGen::LoadConditional(Expr* x, bool is_lhs, Label* ttarget, Label* ftarget) {
  assert(x_.am == AM_NONE);
  NCodeGenState* old_state = state_;
  NCodeGenState new_state(is_lhs, true, 0, down_cast<NLabel*>(ttarget), down_cast<NLabel*>(ftarget));
  state_ = &new_state;
  Visit(x);
  state_ = old_state;
}


// Load the expression as a left-hand side (LHS) value, but not to store into.
// Used to preserve side effects of LHS of dead assignments.
void NCodeGen::LoadLHS(Expr* x) {
  assert(x_.am == AM_NONE);
  NCodeGenState* old_state = state_;
  NCodeGenState new_state(true, true, 0, ttarget(), ftarget());
  state_ = &new_state;
  Visit(x);
  state_ = old_state;
}


// Stores x_ into x
void NCodeGen::Store(Expr* x, int delta) {
  assert(x_.am != AM_CC);
  NCodeGenState* old_state = state_;
  NCodeGenState new_state(true, false, delta, ttarget(), ftarget());
  state_ = &new_state;
  Visit(x);
  state_ = old_state;
}


void NCodeGen::StoreVarDecl(VarDecl* var) {
  assert(x_.am != AM_CC);
  NCodeGenState* old_state = state_;
  NCodeGenState new_state(true, false, delta(), ttarget(), ftarget());
  state_ = &new_state;
  assert(VariableAccess(var->type(), false, is_lhs(), delta()) == storeV);
  AddrMod bp_reg = GetBP(var->level(), RS_ANY);  // bp_reg is reserved
  Operand dst(AM_BASED + bp_reg, kPtrSize, var->offset());
  set_type(&dst, var->type());
  // static variables are only initialized once, so don't bother checking
  // their old value, which is always undef
  // other variables may be initialized in a loop, so check their old
  // value and dec ref it as in a regular assignment
  StoreVal(&dst, &x_, !do_statics());
  state_ = old_state;
}


void NCodeGen::Compare(Operand* tag, Operand* label, Type* type) {
  assert(x_.am == AM_NONE);
  if (type->is_bool() || type->is_int() || type->is_uint() ||
      type->is_fingerprint() || type->is_time())
    CompareBits(eql_bits, tag, label);
  else if (type->is_float())
    CompareFAMTC(eql_float, tag, label);
  else if (type->is_string())
    CompareSB(eql_string, tag, label);
  else if (type->is_bytes())
    CompareSB(eql_bytes, tag, label);
  else
    ShouldNotReachHere();
}


void NCodeGen::Deref(Operand* ptr, Operand* val, size_t val_size, size_t val_offset) {
  // undef checks have been performed by now
  assert(is_szl_val(ptr) && !needs_check(ptr));
  assert(IsIntReg(ptr->am));
  assert(!is_int_val(ptr));

  // dereference val pointer
  val->am = static_cast<AddrMod>(AM_BASED + ptr->am);  // transfer register ownership
  val->size = val_size;
  val->offset = val_offset;
  set_type(val, NULL);  // not a szl val ptr anymore
  // no need to release ptr unless dec ref required
  if (is_ref_incrd(ptr)) {
    // load val in a different register before releasing ptr
    ReserveRegs(val);
    LoadOperand(val, val_size == 1 ? RS_BYTE : RS_ANY);
    ReleaseOperand(ptr);
  } else {
    ptr->Clear();
  }
}


// x_ = *left op *right
// the result is not packaged as a Val* yet, only Val
void NCodeGen::BinaryOp(Operand* left, Operand* right, Opcode op, size_t val_size, size_t val_offset) {
  assert(x_.am == AM_NONE);
  LoadOperand(left, RS_ANY);
  LoadOperand(right, RS_ANY);
  Operand left_val;
  Operand right_val;
  Deref(left, &left_val, val_size, val_offset);
  Deref(right, &right_val, val_size, val_offset);
  LoadOperand(&left_val, val_size == 1 ? RS_BYTE : RS_ANY);
  x_ = left_val;
  ReserveRegs(&x_);
  switch (op) {
    case and_bool:
      asm_.AndRegEA(x_.am, &right_val);
      break;

    case or_bool:
      asm_.OrRegEA(x_.am, &right_val);
      break;

    default:
      ShouldNotReachHere();
  }
  ReleaseOperand(&left_val);
  ReleaseOperand(&right_val);
}


void NCodeGen::CompareInt(Opcode op, Operand* left, Operand* right) {
  assert(x_.am == AM_NONE);
  // optimize case when both operands are smi
  LoadOperand(left, RS_ANY);
  LoadOperand(right, RS_ANY);
  // undef checks and null checks have been performed by now
  assert(is_int_val(left) && IsIntReg(left->am) && !needs_check(left));
  assert(is_int_val(right) && IsIntReg(right->am) && !needs_check(right));
  // if either left or right is not an smi, execute slow case
  NLabel slow_case(proc_);
  if (!is_smi_val(left)) {
    asm_.TestImm(left, TaggedInts::tag_mask);
    Operand left_not_an_smi(AM_CC, CC_E);
    BranchShort(branch_true, &left_not_an_smi, &slow_case);
  }
  if (!is_smi_val(right)) {
    asm_.TestImm(right, TaggedInts::tag_mask);
    Operand right_not_an_smi(AM_CC, CC_E);
    BranchShort(branch_true, &right_not_an_smi, &slow_case);
  }
  // both left and right are smi, compare them directly including TaggedInts::tag_mask
  asm_.CmpRegEA(left->am, right);
  NLabel done(proc_);
  BranchShort(branch, NULL, &done);
  // cc is preserved and must match the cc resulting from the slow case at label done
  Bind(&slow_case);
  CondCode cc = CC_NONE;

  switch (op) {
    case lss_int:
      cc = CC_L;
      break;

    case leq_int:
      cc = CC_LE;
      break;

    case gtr_int:
      cc = CC_G;
      break;

    case geq_int:
      cc = CC_GE;
      break;

    default:
      ShouldNotReachHere();
  }

  RegsState arg_regs;
  PreloadArgs(left, 0, right, 1, 2, &arg_regs);
  ChkFunPtr<int (IntVal* x, IntVal* y)> fun_ptr(NSupport::CmpInt);
  { FunctionCall fc(this, fun_ptr, &arg_regs, SymbolTable::bad_type(), false);
    PushVal(right);
    PushVal(left);
  }
  asm_.TestReg(&x_, x_.am);
  Operand cmp_result(AM_CC, cc);  // same cc for both fast and slow cases
  ReleaseOperand(&x_);
  x_ = cmp_result;
  Bind(&done);
}


void NCodeGen::CompareBits(Opcode op, Operand* left, Operand* right) {
  // There is no guarantee that the basic64 value will be at the same
  // offset in all operand types; we hence call a type-specific support routine
  // instead of inlining the comparison
  // todo or can we make use of the compile-time type info and access
  // the value at the correct offset in inline code? most probably
  // e.g. ValueSize(x->left()->type()), ValueOffset(x->left()->type())

  // optimize eql_bits and neq_bits by comparing left and right directly
  assert(x_.am == AM_NONE);
  NLabel done(proc_);
  // we can optimize an equality test if we don't have to dec_ref left or right
  // todo try to optimize even if is_ref_incrd()
  if ((op == eql_bits || op == neq_bits) &&
      !is_ref_incrd(left) && !is_ref_incrd(right)) {
    LoadOperand(left, RS_ANY);
    LoadOperand(right, RS_ANY);
    // undef checks and null checks have been performed by now
    assert(is_szl_val(left) && IsIntReg(left->am) && !needs_check(left));
    assert(is_szl_val(right) && IsIntReg(right->am) && !needs_check(right));

    // if left and right (smi or not) are equal, we are done
    asm_.CmpRegEA(left->am, right);
    Operand left_eq_right(AM_CC, CC_E);
    if (is_smi_val(left) || is_smi_val(right)) {
      // if left or right is a (compile-time) smi, cc is the result, we are done
      x_ = left_eq_right;
      if (op == neq_bits)
        x_.cc = CC_NE;

      assert(!is_ref_incrd(left) && !is_ref_incrd(right));
      // otherwise, ReleaseOperand would call dec_ref and lose cc
      ReleaseOperand(left);
      ReleaseOperand(right);

      return;
    }
    BranchShort(branch_true, &left_eq_right, &done);
    // we branch and do not dec ref the operands, make sure it's not necessary
    assert(!is_ref_incrd(left) && !is_ref_incrd(right));

    if (is_int_val(left)) {
      assert(is_int_val(right));
      // if left or right is a (run-time) smi, left and right are not equal
      // and we are done, otherwise execute slow case
      // this optimization only works, because smi are never encoded as a ptr
      if (!is_smi_val(left)) {
        asm_.TestImm(left, TaggedInts::tag_mask);
        Operand left_is_smi(AM_CC, CC_NE);
        BranchShort(branch_true, &left_is_smi, &done);
      }
      if (!is_smi_val(right)) {
        asm_.TestImm(right, TaggedInts::tag_mask);
        Operand right_is_smi(AM_CC, CC_NE);
        BranchShort(branch_true, &right_is_smi, &done);
      }
      // cc is preserved and must match the cc resulting from the slow case at
      // label done, note that x_.cc will be set below depending on op

      // both left and right are guaranteed not to be smi at this location
      // since we have already tested for smi, suppress subsequent unneeded
      // smi tests while pushing args below by changing the type to a non
      // integer type, e.g. string_type();
      set_type(left, SymbolTable::string_type());
      set_type(right, SymbolTable::string_type());
    }
  }
  CondCode cc = CC_E;
  bool swap_operands = false;
  FunPtr fun = ChkFunPtr<bool (Val* x, Val* y)>(NSupport::LssBits);

  switch (op) {
    case eql_bits:
      fun = ChkFunPtr<bool (Val* x, Val* y)>(NSupport::EqlBits);
      break;

    case neq_bits:
      fun = ChkFunPtr<bool (Val* x, Val* y)>(NSupport::EqlBits);
      cc = CC_NE;
      break;

    case lss_bits:
      break;

    case leq_bits:
      swap_operands = true;
      cc = CC_NE;
      break;

    case gtr_bits:
      swap_operands = true;
      break;

    case geq_bits:
      cc = CC_NE;
      break;

    default:
      ShouldNotReachHere();
  }

  RegsState arg_regs;
  PreloadArgs(swap_operands ? right : left, 0, swap_operands ? left : right, 1, 2, &arg_regs);
  { FunctionCall fc(this, fun, &arg_regs, SymbolTable::bad_type(), false);
    if (swap_operands) {
      PushVal(left);
      PushVal(right);
    } else {
      PushVal(right);
      PushVal(left);
    }
  }
  Operand true_imm(AM_IMM, 1);
  true_imm.size = sizeof(bool);  // not a szl bool
  LoadOperand(&x_, RS_BYTE);
  asm_.CmpRegEA(x_.am, &true_imm);
  Operand cmp_result(AM_CC, cc);
  ReleaseOperand(&x_);
  x_ = cmp_result;
  Bind(&done);
}


// Compare a pair of string or bytes
void NCodeGen::CompareSB(Opcode op, Operand* left, Operand* right) {
  assert(x_.am == AM_NONE);
  FunPtr fun;
  CondCode cc = CC_NONE;

  switch (op) {
    case eql_string:
      fun = ChkFunPtr<int (StringVal* x, StringVal* y)>(NSupport::EqlString);
      cc = CC_E;
      break;

    case neq_string:
      fun = ChkFunPtr<int (StringVal* x, StringVal* y)>(NSupport::EqlString);
      cc = CC_NE;
      break;

    case lss_string:
      fun = ChkFunPtr<int (StringVal* x, StringVal* y)>(NSupport::CmpString);
      cc = CC_L;
      break;

    case leq_string:
      fun = ChkFunPtr<int (StringVal* x, StringVal* y)>(NSupport::CmpString);
      cc = CC_LE;
      break;

    case gtr_string:
      fun = ChkFunPtr<int (StringVal* x, StringVal* y)>(NSupport::CmpString);
      cc = CC_G;
      break;

    case geq_string:
      fun = ChkFunPtr<int (StringVal* x, StringVal* y)>(NSupport::CmpString);
      cc = CC_GE;
      break;

    case eql_bytes:
      fun = ChkFunPtr<int (BytesVal* x, BytesVal* y)>(NSupport::EqlBytes);
      cc = CC_E;
      break;

    case neq_bytes:
      fun = ChkFunPtr<int (BytesVal* x, BytesVal* y)>(NSupport::EqlBytes);
      cc = CC_NE;
      break;

    case lss_bytes:
      fun = ChkFunPtr<int (BytesVal* x, BytesVal* y)>(NSupport::CmpBytes);
      cc = CC_L;
      break;

    case leq_bytes:
      fun = ChkFunPtr<int (BytesVal* x, BytesVal* y)>(NSupport::CmpBytes);
      cc = CC_LE;
      break;

    case gtr_bytes:
      fun = ChkFunPtr<int (BytesVal* x, BytesVal* y)>(NSupport::CmpBytes);
      cc = CC_G;
      break;

    case geq_bytes:
      fun = ChkFunPtr<int (BytesVal* x, BytesVal* y)>(NSupport::CmpBytes);
      cc = CC_GE;
      break;

    default:
      ShouldNotReachHere();
  }

  RegsState arg_regs;
  PreloadArgs(left, 0, right, 1, 2, &arg_regs);
  { FunctionCall fc(this, fun, &arg_regs, SymbolTable::bad_type(), false);
    PushVal(right);
    PushVal(left);
  }
  asm_.TestReg(&x_, x_.am);
  Operand cmp_result(AM_CC, cc);
  ReleaseOperand(&x_);
  x_ = cmp_result;
}


// Compare a pair of float, uint, array, map, tuple or closure
void NCodeGen::CompareFAMTC(Opcode op, Operand* left, Operand* right) {
  assert(x_.am == AM_NONE);
  CondCode cc = CC_E;
  bool swap_operands = false;
  FunPtr fun;

  switch (op) {
    case eql_float:
      fun = ChkFunPtr<bool (FloatVal* x, FloatVal* y)>(NSupport::EqlFloat);
      break;

    case neq_float:
      fun = ChkFunPtr<bool (FloatVal* x, FloatVal* y)>(NSupport::EqlFloat);
      cc = CC_NE;
      break;

    case lss_float:
      fun = ChkFunPtr<bool (FloatVal* x, FloatVal* y)>(NSupport::LssFloat);
      break;

    case leq_float:
      fun = ChkFunPtr<bool (FloatVal* x, FloatVal* y)>(NSupport::LeqFloat);
      break;

    case gtr_float:
      fun = ChkFunPtr<bool (FloatVal* x, FloatVal* y)>(NSupport::LssFloat);
      swap_operands = true;
      break;

    case geq_float:
      fun = ChkFunPtr<bool (FloatVal* x, FloatVal* y)>(NSupport::LeqFloat);
      swap_operands = true;
      break;

   case eql_array:
      fun = ChkFunPtr<bool (ArrayVal* x, ArrayVal* y)>(NSupport::EqlArray);
      break;

    case neq_array:
      fun = ChkFunPtr<bool (ArrayVal* x, ArrayVal* y)>(NSupport::EqlArray);
      cc = CC_NE;
      break;

    case eql_map:
      fun = ChkFunPtr<bool (MapVal* x, MapVal* y)>(NSupport::EqlMap);
      break;

    case neq_map:
      fun = ChkFunPtr<bool (MapVal* x, MapVal* y)>(NSupport::EqlMap);
      cc = CC_NE;
      break;

    case eql_tuple:
      fun = ChkFunPtr<bool (TupleVal* x, TupleVal* y)>(NSupport::EqlTuple);
      break;

    case neq_tuple:
      fun = ChkFunPtr<bool (TupleVal* x, TupleVal* y)>(NSupport::EqlTuple);
      cc = CC_NE;
      break;

    case eql_closure:
      fun = ChkFunPtr<bool (ClosureVal* x,ClosureVal* y)>(NSupport::EqlClosure);
      break;

    case neq_closure:
      fun = ChkFunPtr<bool (ClosureVal* x,ClosureVal* y)>(NSupport::EqlClosure);
      cc = CC_NE;
      break;

    default:
      ShouldNotReachHere();
  }

  RegsState arg_regs;
  PreloadArgs(swap_operands ? right : left, 0,
              swap_operands ? left : right, 1, 2, &arg_regs);
  { FunctionCall fc(this, fun, &arg_regs, SymbolTable::bad_type(), false);
    if (swap_operands) {
      PushVal(left);
      PushVal(right);
    } else {
      PushVal(right);
      PushVal(left);
    }
  }
  Operand true_imm(AM_IMM, 1);
  true_imm.size = sizeof(bool);  // not a szl bool
  LoadOperand(&x_, RS_BYTE);
  asm_.CmpRegEA(x_.am, &true_imm);
  Operand cmp_result(AM_CC, cc);
  ReleaseOperand(&x_);
  x_ = cmp_result;
}


// Remove a result of the given type from the result operand.
void NCodeGen::DiscardResult(Type* type) {
  if (type->size() > 0)
    ReleaseOperand(&x_);
  else
    assert(x_.am == AM_NONE);
}


// ----------------------------------------------------------------------------
// Statement code

void NCodeGen::Execute(Statement* stat) {
  assert(x_.am == AM_NONE);
  int starting_offset = asm_.esp_offset();  // can be non-zero if we're in a StatExpr.
  // the switch tag allocated in a callee-saved reg may be live here
  assert((regs_.live() & RS_CALLER_SAVED) == RS_EMPTY);
  Visit(stat);
  assert(x_.am == AM_NONE);
  assert(asm_.esp_offset() == starting_offset);
  assert((regs_.live() & RS_CALLER_SAVED) == RS_EMPTY);
}


// ----------------------------------------------------------------------------
// Visitor functionality
// expressions

// Returns the support function implementing the given opcode
NCodeGen::FunPtr NCodeGen::BinarySupportFunction(Opcode op) {
  switch (op) {
    case mul_int:
      return ChkFunPtr<Val* (Proc* proc, IntVal* x, IntVal* y)>(NSupport::MulInt);

    case div_int:
      return ChkFunPtr<Val* (Proc* proc, IntVal* x, IntVal* y)>(NSupport::DivInt);

    case mod_int:
      return ChkFunPtr<Val* (Proc* proc, IntVal* x, IntVal* y)>(NSupport::RemInt);

    case shl_int:
      return ChkFunPtr<Val* (Proc* proc, IntVal* x, IntVal* y)>(NSupport::ShlInt);

    case shr_int:
      return ChkFunPtr<Val* (Proc* proc, IntVal* x, IntVal* y)>(NSupport::ShrInt);

    case and_int:
      return ChkFunPtr<Val* (Proc* proc, IntVal* x, IntVal* y)>(NSupport::AndInt);

    case or_int:
      return ChkFunPtr<Val* (Proc* proc, IntVal* x, IntVal* y)>(NSupport::OrInt);

    case xor_int:
      return ChkFunPtr<Val* (Proc* proc, IntVal* x, IntVal* y)>(NSupport::XorInt);

    case add_float:
      return ChkFunPtr<Val* (Proc* proc, FloatVal* x, FloatVal* y)>(NSupport::AddFloat);

    case sub_float:
      return ChkFunPtr<Val* (Proc* proc, FloatVal* x, FloatVal* y)>(NSupport::SubFloat);

    case mul_float:
      return ChkFunPtr<Val* (Proc* proc, FloatVal* x, FloatVal* y)>(NSupport::MulFloat);

    case div_float:
      return ChkFunPtr<Val* (Proc* proc, FloatVal* x, FloatVal* y)>(NSupport::DivFloat);

    case add_fpr:
      return ChkFunPtr<Val* (Proc* proc, FingerprintVal* x, FingerprintVal* y)>(NSupport::AddFpr);

    case add_array:
      return ChkFunPtr<Val* (Proc* proc, ArrayVal* x, ArrayVal* y)>(NSupport::AddArray);

    case add_bytes:
      return ChkFunPtr<Val* (Proc* proc, BytesVal* x, BytesVal* y)>(NSupport::AddBytes);

    case add_string:
      return ChkFunPtr<Val* (Proc* proc, StringVal* x, StringVal* y)>(NSupport::AddString);

    case add_time:
      return ChkFunPtr<Val* (Proc* proc, TimeVal* x, TimeVal* y)>(NSupport::AddTime);

    case sub_time:
      return ChkFunPtr<Val* (Proc* proc, TimeVal* x, TimeVal* y)>(NSupport::SubTime);

    case add_uint:
      return ChkFunPtr<Val* (Proc* proc, UIntVal* x, UIntVal* y)>(NSupport::AddUInt);

    case sub_uint:
      return ChkFunPtr<Val* (Proc* proc, UIntVal* x, UIntVal* y)>(NSupport::SubUInt);

    case mul_uint:
      return ChkFunPtr<Val* (Proc* proc, UIntVal* x, UIntVal* y)>(NSupport::MulUInt);

    case div_uint:
      return ChkFunPtr<Val* (Proc* proc, UIntVal* x, UIntVal* y)>(NSupport::DivUInt);

    case mod_uint:
      return ChkFunPtr<Val* (Proc* proc, UIntVal* x, UIntVal* y)>(NSupport::ModUInt);

    case shl_uint:
      return ChkFunPtr<Val* (Proc* proc, UIntVal* x, UIntVal* y)>(NSupport::ShlUInt);

    case shr_uint:
      return ChkFunPtr<Val* (Proc* proc, UIntVal* x, UIntVal* y)>(NSupport::ShrUInt);

    case and_uint:
      return ChkFunPtr<Val* (Proc* proc, UIntVal* x, UIntVal* y)>(NSupport::AndUInt);

    case or_uint:
      return ChkFunPtr<Val* (Proc* proc, UIntVal* x, UIntVal* y)>(NSupport::OrUInt);

    case xor_uint:
      return ChkFunPtr<Val* (Proc* proc, UIntVal* x, UIntVal* y)>(NSupport::XorUInt);

    default:
      ;
  }

  ShouldNotReachHere();
  return FunPtr();
}


void NCodeGen::DoBinary(Binary* x) {
  Trace t(&tlevel_, "(Binary");
  if (x->op() == Binary::LAND) {
    NLabel is_true(proc_);
    LoadConditional(x->left(), false, &is_true, ftarget());
    Branch(branch_false, ftarget());
    Bind(&is_true);
    LoadConditional(x->right(), false, ttarget(), ftarget());
  } else if (x->op() == Binary::LOR) {
    NLabel is_false(proc_);
    LoadConditional(x->left(), false, ttarget(), &is_false);
    Branch(branch_true, ttarget());
    Bind(&is_false);
    LoadConditional(x->right(), false, ttarget(), ftarget());
  } else {
    assert(x_.am == AM_NONE);
    Load(x->left(), false);
    Operand left = x_;
    x_.Clear();
    Operand right;
    ProtectAndLoad(&left, x->right(), false, &right);
    // function calls in left and right expressions have been executed, OK to
    // load left and right values in regs now without increasing their ref count

    switch (x->opcode()) {
      case and_bool:
      case or_bool:
        { BinaryOp(&left, &right, x->opcode(),
                   BoolVal::val_size(), BoolVal::val_offset());
          asm_.TestReg(&x_, x_.am);
          assert(!is_ref_incrd(&x_));
          // otherwise, ReleaseOperand would call dec_ref and lose cc
          ReleaseOperand(&x_);
          Operand not_zero(AM_CC, CC_NE);
          x_ = not_zero;
        }
        return;

      case add_int:
      case sub_int:
        { assert(x_.am == AM_NONE);
          // constant folding
          if (left.am == AM_IMM && right.am == AM_IMM) {
            const szl_int lval = reinterpret_cast<IntVal*>(left.value)->val();
            const szl_int rval = reinterpret_cast<IntVal*>(right.value)->val();
            const szl_int val = x->opcode() == add_int ? lval + rval : lval - rval;
            Operand result(AM_IMM, Factory::NewInt(proc_, val));
            set_type(&result, x->type());
            x_ = result;
            return;
          }
          // attempt the fast case with 2 smi, falling back to slow case if needed
          // do not overwrite left, since it may be needed in slow case
          Operand result = left;
          ReserveRegs(&result);
          // fast case falls back to slow case if undef; slow case checks for undef
          clear_flags(&result, kCheckUndef);
          LoadOperand(&result, RS_ANY & ~Regs(left.am));

          NLabel slow_case(proc_);
          NLabel fast_case(proc_);
          NLabel done(proc_);
          if (x->opcode() == add_int) {
            // we don't try to optimize the rare case where one of the operands is
            // an immediate val pointer (but not an smi)
            asm_.AddRegEA(result.am, &right);  // sets CC

            Operand overflow(AM_CC, CC_O);
            BranchShort(branch_true, &overflow, &slow_case);
            asm_.AddImmReg(result.am, -2);
            asm_.TestImm(&result, TaggedInts::tag_mask);
            Operand fits_smi(AM_CC, CC_E);
            BranchShort(branch_true, &fits_smi, &fast_case);

          } else {
            assert(x->opcode() == sub_int);

            // if left operand is not an smi, jump to slow case
            if (!is_smi_val(&result)) {
              // no need to generate the test if we know at compile time that the
              // left operand is an smi
              asm_.TestImm(&result, TaggedInts::tag_mask);
              Operand no_smi(AM_CC, CC_E);
              BranchShort(branch_true, &no_smi, &slow_case);
            }

            if (right.am == AM_IMM) {
              // we don't try to optimize the rare case where one of the operands is
              // an immediate val pointer (but not an smi)
              if ((right.value & TaggedInts::tag_mask) == TaggedInts::ptr_tag)
                BranchShort(branch, &slow_case);

              asm_.SubRegEA(result.am, &right);  // sets CC
            } else {
              Operand right_smi = right;
              ReserveRegs(&right_smi);
              // fast case falls back to slow case if undef; slow case checks for undef
              clear_flags(&right_smi, kCheckUndef);
              // do not overwrite right, since this code is not always executed
              LoadOperand(&right_smi, RS_ANY & ~Regs(right.am));

              // if right operand is not an smi, jump to slow case
              if (!is_smi_val(&right_smi)) {
                // no need to generate the test if we know at compile time that the
                // right operand is an smi
                asm_.TestImm(&right_smi, TaggedInts::tag_mask);
                Operand no_smi(AM_CC, CC_E);
                BranchShort(branch_true, &no_smi, &slow_case);
              }

              asm_.SubRegEA(result.am, &right);  // sets CC
              ReleaseRegs(&right_smi);
            }
            Operand overflow(AM_CC, CC_O);
            BranchShort(branch_true, &overflow, &slow_case);
            asm_.Inc(&result);  // set smi_tag
            BranchShort(branch, NULL, &done);
          }

          Bind(&slow_case);
          ReleaseRegs(&result);  // no fast case result
          { RegsState arg_regs;
            PreloadArgs(&left, 1, &right, 2, 3, &arg_regs);
            ChkFunPtr<Val* (Proc* proc, IntVal* x, IntVal* y)>
              fun_ptr(x->opcode() == add_int ? NSupport::AddInt
                                             : NSupport::SubInt);
            FunctionCall fc(this, fun_ptr, &arg_regs, x->type(), false);
            PushVal(&right);
            PushVal(&left);
          }
          // use same register for result as in fast case
          ReleaseRegs(&x_);
          asm_.MoveRegReg(result.am, x_.am);
          x_.am = result.am;
          ReserveRegs(&x_);

          if (x->opcode() == add_int) {
            BranchShort(branch, NULL, &done);
            // finish fast case by setting smi tag
            Bind(&fast_case);
            asm_.Inc(&x_);  // set smi_tag
          }

          // whether in fast or slow case, the result cannot be undef, no checks
          clear_flags(&x_, kCheckUndef | kCheckNull);
          // type flags already set
          // kRefIncrd flag set, ignored if an smi

          Bind(&done);
        }
        return;

      case mul_int:
      case div_int:
      case mod_int:
      case shl_int:
      case shr_int:
      case and_int:
      case or_int:
      case xor_int:
      case add_float:
      case sub_float:
      case mul_float:
      case div_float:
      case add_fpr:
      case add_array:
      case add_bytes:
      case add_string:
      case add_time:
      case sub_time:
      case add_uint:
      case sub_uint:
      case mul_uint:
      case div_uint:
      case mod_uint:
      case shl_uint:
      case shr_uint:
      case and_uint:
      case or_uint:
      case xor_uint:
        { RegsState arg_regs;
          PreloadArgs(&left, 1, &right, 2, 3, &arg_regs);
          { FunctionCall fc(this, BinarySupportFunction(x->opcode()),
                            &arg_regs, x->type(), x->CanCauseTrap(false));
            PushVal(&right);
            PushVal(&left);
          }
          // the result will be undef and trap_info set if the divisor was zero
          if (x->CanCauseTrap(false))
            TrapIfInfo(false);
        }
        return;

      case eql_bits:
      case neq_bits:
      case lss_bits:
      case leq_bits:
      case gtr_bits:
      case geq_bits:
        CompareBits(x->opcode(), &left, &right);
        return;

      case lss_int:
      case leq_int:
      case gtr_int:
      case geq_int:
        CompareInt(x->opcode(), &left, &right);
        return;

      case eql_string:
      case neq_string:
      case lss_string:
      case leq_string:
      case gtr_string:
      case geq_string:
      case eql_bytes:
      case neq_bytes:
      case lss_bytes:
      case leq_bytes:
      case gtr_bytes:
      case geq_bytes:
        CompareSB(x->opcode(), &left, &right);
        return;

      case eql_float:
      case neq_float:
      case lss_float:
      case leq_float:
      case gtr_float:
      case geq_float:
      case eql_array:
      case neq_array:
      case eql_map:
      case neq_map:
      case eql_tuple:
      case neq_tuple:
      case eql_closure:
      case neq_closure:
        CompareFAMTC(x->opcode(), &left, &right);
        return;

      default:
        ;
    }
    ShouldNotReachHere();
  }
}


void NCodeGen::InlineLenIntrinsic(Operand* operand, Type* type) {
  assert(type->is_indexable());
  LoadOperand(operand, RS_ANY);
  Operand length;
  if (type->is_string())
    Deref(operand, &length, StringVal::num_runes_size(), StringVal::num_runes_offset());
  else
    Deref(operand, &length, IndexableVal::length_size(), IndexableVal::length_offset());

  // todo no check that length fits in an smi (<= 536'870'911 in 32-bit mode)
  // should it be the responsibility of IndexableVal to ensure that?
  LoadOperand(&length, RS_ANY);
  // The length and num_runes fields are "int", but a Val* is pointer
  // sized.  So on 64-bit machines we need to extend the value from
  // 32 bits to 64 bits before modifying it.
  length.size = kPtrSize;
  asm_.ShiftRegLeft(length.am, 2);
  asm_.Inc(&length);  // set smi_tag
  set_type(&length, SymbolTable::int_type());
  // no need to set flags to check for undef or null
  x_ = length;
}


void NCodeGen::DoCall(Call* x) {
  Trace t(&tlevel_, "(Call");
  const List<Expr*>* args = x->args();
  const bool check_err = x->CanCauseTrap(false);
  if (x->fun()->AsIntrinsic() != NULL) {
    Intrinsic* fun = x->fun()->AsIntrinsic();

    // handle some special intrinsics
    // (typically require special argument handling)
    if (fun->kind() == Intrinsic::DEBUG) {
      // DEBUG() is very special.
      // we know we have at least one argument and that it is a string literal
      string cmd = args->at(0)->as_string()->cpp_str(proc_);
      if (cmd == "print") {
        // for debugging: print values
        // We need to push the args in forward order, because the stub will
        // reverse their order when pushing them on the interpreter stack
        const int num_args = args->length() - 1;
        ChkFunPtr<Val* (Proc* proc, int fd, int num_args, ...)>
            fun_ptr(NSupport::FdPrint, num_args);
        { FunctionCall fc(this, fun_ptr, NULL, x->type(), false);
          PushExprs(args, 1, num_args);
          Operand num_args_imm(AM_IMM, num_args);
          PushOperand(&num_args_imm);
          Operand fd_imm(AM_IMM, 1);
          PushOperand(&fd_imm);
        }
        return;
      }
      if (cmd == "ref") {
        // for debugging: print ref count of a value
        assert(x_.am == AM_NONE);
        ChkFunPtr<Val* (Proc* proc, Val* val)> fun_ptr(NSupport::DebugRef);
        { FunctionCall fc(this, fun_ptr, NULL, SymbolTable::int_type(), false);
          PushExpr(args->at(1), false);
        }
        return;
      }
      Unimplemented();
      return;
    }

    if (fun->kind() == Intrinsic::DEF) {
      // Try to avoid undef trap by generating an explicit test.
      // The trap happens anyway in some cases (e.g. undefined index i in a[i]).
      // In 64-bit mode, we may have live caller-saved registers that could
      // get trashed by the trap handler; save and restore them.
      RegsState saved_regs(regs_);
      saved_regs.ReleaseRegs(~RS_CALLER_SAVED & RS_ANY);  // keep only caller-saved
      // generate more efficient code if no registers need to be saved/restored
      bool none_saved = saved_regs.live() == RS_EMPTY;
      if (!none_saved) {
        asm_.PushRegs(saved_regs.live());
        regs_.ReleaseRegs(&saved_regs);  // saved regs are not live anymore
      }
      NLabel undefined(proc_);
      { NTrapHandler handler(this, none_saved ? ftarget() : &undefined, NULL,
                             true, args->at(0));
        // evaluate expr & throw away result
        Load(args->at(0), false);  // may trap
        if (!needs_undef_check(&x_)) {
          // x_ cannot be undefined, since it does not require an undef check
          ReleaseOperand(&x_);

        } else {
          clear_flags(&x_, kCheckUndef);  // suppress undef check during load
          LoadOperand(&x_, RS_ANY);
          asm_.TestReg(&x_, x_.am);
          // generate more efficient code if there are no registers to restore
          // and if ref_count does not need adjustment
          if (none_saved && !is_ref_incrd(&x_)) {
            ReleaseOperand(&x_);  // generates no code
            Operand is_def(AM_CC, CC_NE);
            x_ = is_def;
            return;

          } else {
            Operand is_undef(AM_CC, CC_E);
            Operand def_x = x_;  // we save x_ to be released after the test
            x_ = is_undef;
            Branch(branch_true, none_saved ? ftarget() : &undefined);
            // at this point, we know that x_ was defined and we can release it
            // no null check is needed during dec_ref, called if kRefIncrd set
            clear_flags(&def_x, kCheckNull);
            ReleaseOperand(&def_x);
          }
        }
      }
      // if we reach this point, the expression was defined
      if (none_saved) {
        Operand is_def(AM_CC, CC_TRUE);  // always true
        x_ = is_def;
      } else {
        int saved_esp_offset = asm_.esp_offset();
        asm_.PopRegs(saved_regs.live());
        regs_.ReserveRegs(&saved_regs);  // restored regs are live again
        Branch(branch, ttarget());
        // both code paths are restoring saved registers, adjust state
        asm_.set_esp_offset(saved_esp_offset);  // saved regs not popped yet
        regs_.ReleaseRegs(&saved_regs);  // saved regs are not live yet
        // trap handler will continue here
        Bind(&undefined);
        asm_.PopRegs(saved_regs.live());
        regs_.ReserveRegs(&saved_regs);  // restored regs are live again
        Operand is_def(AM_CC, CC_FALSE);  // x is undefined at this point
        x_ = is_def;  // always false
      }
      return;
    }

    if (fun->kind() == Intrinsic::INPROTO ||
        fun->kind() == Intrinsic::CLEARPROTO) {
      // because the argument is a selector, the
      // selector's variable type (t) must be a tuple
      Selector* s = args->at(0)->AsSelector();
      TupleType* t = s->var()->type()->as_tuple();
      assert(t != NULL);
      // compute the bit offset i for the inproto bit
      // in the tuple's bit vector (following the fields)
      int i = t->inproto_index(s->field());
      // customize based on intrinsic
      bool is_inproto = (fun->kind() == Intrinsic::INPROTO);
      bool make_unique = !is_inproto;
      Type* result_type = is_inproto ? SymbolTable::bad_type() : NULL;
      FunPtr fun_ptr;
      if (is_inproto)
        fun_ptr = ChkFunPtr<bool (int i, TupleVal* t)>(NSupport::FTestB);
      else
        fun_ptr = ChkFunPtr<void (int i, TupleVal* t)>(NSupport::FClearB);
      // emit code
      { FunctionCall fc(this, fun_ptr, NULL, result_type, false);
        PushExpr(s->var(), make_unique);
        Operand bit_imm(AM_IMM, i);
        PushOperand(&bit_imm);
      }
      if (fun->kind() == Intrinsic::INPROTO) {
        Operand true_imm(AM_IMM, 1);
        true_imm.size = sizeof(bool);  // not a szl bool
        LoadOperand(&x_, RS_BYTE);
        asm_.CmpRegEA(x_.am, &true_imm);
        Operand cmp_result(AM_CC, CC_E);
        ReleaseOperand(&x_);
        x_ = cmp_result;
      }
      return;
    }

    if (fun->kind() == Intrinsic::UNDEFINE) {
      Variable* v = args->at(0)->AsVariable();
      assert(v != NULL);
      AddrMod bp_reg = GetBP(v->level(), RS_ANY);  // bp_reg is reserved
      Operand dst(AM_BASED + bp_reg, kPtrSize, v->offset());
      set_type(&dst, v->type());
      Operand null(AM_IMM, static_cast<void*>(NULL));
      set_type(&null, v->type());
      clear_flags(&null, kCheckUndef | kCheckNull);
      assert(is_szl_val(&null));  // so that StoreVal does not choke on IncRefOperand
      assert(x_.am == AM_NONE);
      x_ = null;
      StoreVal(&dst, &x_, true);
      return;
    }

    if (fun->kind() == Intrinsic::LEN) {
      // inline this intrinsic instead of generating a call
      Type* type = args->at(0)->type();
      if (type->is_indexable()) {
        Load(args->at(0), false);
        InlineLenIntrinsic(&x_, type);
        return;
      } else if (type->is_map()) {
        // harder to inline (method call, 64-bit result), skip optimization
        // int64 length = x_->as_map()->occupancy()
        // no return, will continue below and generate a call to intrinsic
      } else {
        ShouldNotReachHere();
      }
    }

    // The type here is just flag indicating whether we need to handle
    // reference counting on the result.
    // TODO: look into this!
    Type* can_fail_type = check_err ? SymbolTable::bad_type() : NULL;
    if (fun->kind() == Intrinsic::SORT ||
        fun->kind() == Intrinsic::SORTX) {
      assert(fun->function() != NULL);
      AddrMod isp;
      { FunctionCall fc(this, CFunPtr(fun->function()), NULL, can_fail_type,
                        check_err);
        // We push the args directly onto the interpreter stack, in reverse order.
        // supply placeholder for missing second argument
        if (args->length() == 1) {
          Operand null_imm(AM_IMM, 0);
          IPushOperand(&null_imm);
        } else {
          IPushExpr(args->at(1));
        }

        IPushExpr(args->at(0));

        PushISPAddr(&isp);
      }
      // Pop result if any
      IPopVal(x->type(), isp, 2, check_err);

      return;
    }

    // special cases - match*() has precompiled pattern (or NULL)
    // Match intrinsics take an additional void* pattern
    typedef const char* (MatchFunction)(Proc* proc, Val**& sp, void* pattern);
    MatchFunction* target;

    switch (fun->kind()) {
      case Intrinsic::MATCH:
        target = Intrinsics::Match;
        break;

      case Intrinsic::MATCHPOSNS:
        target = Intrinsics::Matchposns;
        break;

      case Intrinsic::MATCHSTRS:
        target = Intrinsics::Matchstrs;
        break;

      default:
        target = NULL;
    }

    AddrMod isp;
    if (target != NULL) {
      ChkFunPtr<MatchFunction> fun_ptr(target);
      FunctionCall fc(this, fun_ptr, NULL, can_fail_type, check_err);

      // We push the args directly onto the interpreter stack, in reverse order.
      IPushReverseExprs(args, args->length());

      Operand pattern_imm(AM_IMM, CompiledRegexp(args->at(0), proc_, &error_count_));
      PushOperand(&pattern_imm);

      PushISPAddr(&isp);

    } else {
      // find the target, mapping overloaded intrinsics as needed.
      Intrinsic::CFunction target = Intrinsics::TargetFor(proc_, fun, args);
      FunctionCall fc(this, CFunPtr(target), NULL, can_fail_type, check_err);

      // We push the args directly onto the interpreter stack, in reverse order.
      IPushReverseExprs(args, args->length());

      PushISPAddr(&isp);
    }
    // Pop result if any
    IPopVal(x->type(), isp, args->length(), check_err);

  } else {
    // Regular function call
    { FunctionCall fc(this, FunPtr(x->fun()), NULL, x->type(), check_err);
      PushReverseExprs(args, args->length());
    }
  }
  // note: unused results, if any, will be discarded by DoExprStat
}


void NCodeGen::DoComposite(Composite* x) {
  Trace t(&tlevel_, "(Composite");
  // used to construct array, bytes, map, string and tuple literals.
  assert(!x->type()->is_incomplete());

  // issue creation calls
  Type* type = x->type();
  const int num_args = x->length();
  if (type->is_array()) {
    InitializeArray(x, 0, num_args);
  } else if (type->is_bytes()) {
    ChkFunPtr<Val* (Proc* proc, int num_args, ...)>
      fun_ptr(NSupport::CreateB, num_args);
    { FunctionCall fc(this, fun_ptr, NULL, type, false);

      PushReverseExprs(x, num_args);

      Operand num_args_imm(AM_IMM, num_args);
      PushOperand(&num_args_imm);
    }
  } else if (type->is_map()) {
    InitializeMap(x, 0, num_args);
  } else if (type->is_string()) {
    ChkFunPtr<Val* (Proc* proc, int num_args, ...)>
      fun_ptr(NSupport::CreateStr, num_args);
    { FunctionCall fc(this, fun_ptr, NULL, type, true);

      PushReverseExprs(x, num_args);

      Operand num_args_imm(AM_IMM, num_args);
      PushOperand(&num_args_imm);
    }
    TrapIfInfo(false);
  } else if (type->is_tuple()) {
    InitializeTuple(x, 0, num_args);
  } else {
    ShouldNotReachHere();
  }
}


// Initialize an array in pieces no larger than kMaxNumCompositeElems.
// Recursively initialize the "rest" of the array first, then initialize
// the piece beginning at from_arg.  This ordering maintains the stack
// discipline of the code generator because the array value is produced
// directly into its argument position on the stack but also puts an
// upper bound on the total stack size.
//
// For example, if the max number was 2, the array {1, 2, 3, 4, 5, 6}
// would be initialized with the sequence:
// InitA(1, 2, InitA(3, 4, InitA(5, 6, CreateA())))
void NCodeGen::InitializeArray(Composite* x, int from_arg, int num_args) {
  assert(from_arg <= num_args);
  Type* type = x->type();
  if (from_arg == num_args) {
    // Base case: create the array empty.
    ChkFunPtr<Val* (Proc* proc, ArrayType* atype, int num_args)>
      fun_ptr(NSupport::CreateA);
    FunctionCall fc(this, fun_ptr, NULL, type, false);

    Operand num_args_imm(AM_IMM, num_args);
    PushOperand(&num_args_imm);

    Operand type_imm(AM_IMM, type);
    PushOperand(&type_imm);
  } else {
    const int num_vals = std::min(num_args - from_arg, kMaxNumCompositeElems);
    ChkFunPtr<Val* (Proc* proc,  int from_val, int num_vals, ...)>
      fun_ptr(NSupport::InitA, num_vals + 1);
    FunctionCall fc(this, fun_ptr, NULL, type, false);

    // Initialize the rest of the array first.
    InitializeArray(x, from_arg + num_vals, num_args);
    PushVal(&x_);

    // Initialize our chunk.
    PushReverseExprs(x, from_arg, num_vals);

    Operand num_vals_imm(AM_IMM, num_vals);
    PushOperand(&num_vals_imm);

    Operand from_arg_imm(AM_IMM, from_arg);
    PushOperand(&from_arg_imm);
  }
}


// Initialize a map in pieces no larger than kMaxNumCompositeElems.
// Recursively initialize the "rest" of the map first, then initialize
// the piece beginning at from_arg.  This ordering maintains the stack
// discipline of the code generator because the map value is produced
// directly into its argument position on the stack but also puts an
// upper bound on the total stack size.
//
// For example, if the max number was 2, the map {"a":1, "b":2, "c":3}
// would be initialized with the sequence:
// InitM("a", 1, InitM("b", 2, InitM("c", 3, CreateM())))
void NCodeGen::InitializeMap(Composite* x, int from_arg, int num_args) {
  assert(from_arg <= num_args);
  assert(from_arg % 2 == 0);
  assert(num_args % 2 == 0);
  Type* type = x->type();
  if (from_arg == num_args) {
    // Base case: create the map empty.
    ChkFunPtr<Val* (Proc* proc, MapType* mtype, int num_args)>
      fun_ptr(NSupport::CreateM);
    FunctionCall fc(this, fun_ptr, NULL, type, false);

    Operand num_args_imm(AM_IMM, num_args/2);
    PushOperand(&num_args_imm);

    Operand type_imm(AM_IMM, type);
    PushOperand(&type_imm);
  } else {
    const int num_vals = std::min(num_args - from_arg, kMaxNumCompositeElems);
    ChkFunPtr<Val* (Proc* proc, int num_vals, ...)>
      fun_ptr(NSupport::InitM, num_vals + 1);
    FunctionCall fc(this, fun_ptr, NULL, type, false);

    // Initialize the rest of the map first.
    InitializeMap(x, from_arg + num_vals, num_args);
    PushVal(&x_);

    PushReverseExprs(x, from_arg, num_vals);

    Operand num_vals_imm(AM_IMM, num_vals);
    PushOperand(&num_vals_imm);
  }
}

// Initialize a tuple in pieces no larger than kMaxNumCompositeElems.
// Recursively initialize the "rest" of the tuple first, then initialize
// the piece beginning at from_arg.  This ordering maintains the stack
// discipline of the code generator because the tuple value is produced
// directly into its argument position on the stack but also puts an
// upper bound on the total stack size.
//
// For example, if the max number was 2, the tuple {1, 2, 3, 4, 5, 6}
// would be initialized with the sequence:
// InitT(1, 2, InitT(3, 4, InitT(5, 6, CreateT())))
void NCodeGen::InitializeTuple(Composite* x, int from_arg, int num_args) {
  assert(from_arg <= num_args);
  Type* type = x->type();
  if (from_arg == num_args) {
    // Base case: create the tuple empty.
    ChkFunPtr<Val* (Proc* proc, TupleType* ttype)> fun_ptr(NSupport::CreateT);
    FunctionCall fc(this, fun_ptr, NULL, type, false);

    Operand type_imm(AM_IMM, type);
    PushOperand(&type_imm);
  } else {
    const int num_vals = std::min(num_args - from_arg, kMaxNumCompositeElems);
    ChkFunPtr<Val* (Proc* proc, int from_val, int num_vals, ...)>
      fun_ptr(NSupport::InitT, num_vals + 1);
    FunctionCall fc(this, fun_ptr, NULL, type, false);

    // Initialize the rest of the tuple first.
    InitializeTuple(x, from_arg + num_vals, num_args);
    PushVal(&x_);

    PushReverseExprs(x, from_arg, num_vals);

    Operand num_vals_imm(AM_IMM, num_vals);
    PushOperand(&num_vals_imm);

    Operand from_arg_imm(AM_IMM, from_arg);
    PushOperand(&from_arg_imm);
  }
}


void NCodeGen::DoConversion(Conversion* x) {
  Trace t(&tlevel_, "(Conversion op = %s", ConversionOp2String(x->op()));
  const bool check_err = x->CanCauseTrap(false);
  Type* can_fail_type = check_err ? SymbolTable::bad_type() : NULL;
  AddrMod isp;
  if (x->kind() == Conversion::kBasicConv) {
    ChkFunPtr<const char* (Proc* proc, ConversionOp op, Val**& sp, Type* type)>
      fun_ptr(ConvOp::ConvertBasic);
    FunctionCall fc(this, fun_ptr, NULL, can_fail_type, check_err);

    // We push the args directly onto the interpreter stack, in reverse order.
    IPushReverseExprs(x->params(), x->params()->length());

    IPushExpr(x->src());

    Operand type_imm(AM_IMM, x->op() == proto2bytes
                             ? x->src()->type() : x->type());
    PushOperand(&type_imm);

    PushISPAddr(&isp);

    Operand op_imm(AM_IMM, x->op());
    PushOperand(&op_imm);

  } else if (x->kind() == Conversion::kArrayToArrayConv) {
    assert(ImplementedArrayToArrayConversion(x->op()));
    ChkFunPtr<const char* (Proc* proc, ConversionOp op, Val**& sp,
                           ArrayType* type)> fun_ptr(ConvOp::ConvertArray);
    FunctionCall fc(this, fun_ptr, NULL, can_fail_type, check_err);

    // We push the args directly onto the interpreter stack, in reverse order.
    IPushReverseExprs(x->params(), x->params()->length());

    IPushExpr(x->src());

    Operand type_imm(AM_IMM, x->op() == proto2bytes
                             ? x->src()->type() : x->type());
    PushOperand(&type_imm);

    PushISPAddr(&isp);

    Operand op_imm(AM_IMM, x->op());
    PushOperand(&op_imm);

  } else {
    assert(x->kind() == Conversion::kArrayToMapConv);
    assert(x->params()->is_empty());  // too hard otherwise
    assert(ImplementedArrayToMapConversion(x->op()));
    assert(ImplementedArrayToMapConversion(x->key_op()));
    ChkFunPtr<const char* (Proc* proc, MapType* map_type, ConversionOp key_op,
                           ConversionOp value_op, Val**& sp)>
      fun_ptr(ConvOp::ConvertArrayToMap);
    FunctionCall fc(this, fun_ptr, NULL, can_fail_type, check_err);

    // We push the args directly onto the interpreter stack, in reverse order.
    IPushReverseExprs(x->params(), x->params()->length());

    IPushExpr(x->src());

    PushISPAddr(&isp);

    Operand value_op_imm(AM_IMM, x->op());
    PushOperand(&value_op_imm);

    Operand key_op_imm(AM_IMM, x->key_op());
    PushOperand(&key_op_imm);

    Operand type_imm(AM_IMM, x->type());
    PushOperand(&type_imm);
  }
  // Pop result if any
  IPopVal(x->type(), isp, x->params()->length() + 1, check_err);
}


void NCodeGen::DoDollar(Dollar* x) {
  Trace t(&tlevel_, "(Dollar");
  assert(x_.am == AM_NONE);
  if (x->AsComposite() != NULL) {
    Operand literal(AM_IMM, TaggedInts::MakeVal(x->AsComposite()->length()));
    set_type(&literal, x->type());
    x_ = literal;
  } else if (x->length_temp() != NULL) {
    Visit(x->length_temp());
  } else {
    Load(x->array(), false);
    InlineLenIntrinsic(&x_, x->array()->type());
  }
}


void NCodeGen::DoFunction(Function* x) {
  Trace t(&tlevel_, "(Function");
  assert(x_.am == AM_NONE);
  // x->entry()->target() is the offset of the function entry in the code buffer
  // function entry is always at esp_offset 0
  assert(x->entry()->is_bound());
  int entry = down_cast<NLabel*>(x->entry())->target();
  ChkFunPtr<Val* (Proc* proc, FunctionType* ftype, int entry, Frame* context)>
      fun_ptr(NSupport::CreateC);
  { FunctionCall fc(this, fun_ptr, NULL, x->type(), false);

    PushBP(x->context_level());  // static link

    Operand entry_imm(AM_IMM, entry);
    PushOperand(&entry_imm);

    Operand ftype_imm(AM_IMM, x->type());
    PushOperand(&ftype_imm);
  }
}


void NCodeGen::DoSelector(Selector* x) {
  Trace t(&tlevel_, "(Selector");
  TupleType* tuple = x->var()->type()->as_tuple();
  assert(tuple != NULL);
  Opcode op = SelectorAccess(x->field()->type(), is_load(), is_lhs(), delta());
  Operand val;
  Operand var;
  if (op == fstoreV) {
    // operand x_ is the value to store
    assert(x_.am != AM_NONE);
    val = x_;
    x_.Clear();
  } else {
    assert(x_.am == AM_NONE);
  }
  ProtectAndLoad(&val, x->var(), is_lhs(), &var);
  assert(is_szl_val(&var));

  // set the inproto bit if necessary
  if (is_lhs()) {
    LoadOperand(&var, RS_CALLEE_SAVED);  // avoid a reg save across FSetB call
    assert(x_.am == AM_NONE);
    Operand var_clone = var;
    ReserveRegs(&var_clone);
    clear_flags(&var, kRefIncrd);  // cannot skip inc ref twice
    ChkFunPtr<void (int i, TupleVal* t)> fun_ptr(NSupport::FSetB);
    FunctionCall fc(this, fun_ptr, NULL, NULL, false);

    PushVal(&var_clone);

    Operand bit_imm(AM_IMM, tuple->inproto_index(x->field()));
    PushOperand(&bit_imm);
  }

  LoadOperand(&var, RS_ANY);  // no-op if loaded in a callee-saved reg above
  const size_t slot_offset = TupleVal::slot_offset(x->field()->slot_index());
  assert(IsIntReg(var.am));
  Operand field(AM_BASED + var.am, kPtrSize, slot_offset);
  ReserveRegs(&field);  // takes ownership of register var.am released on next line
  ReleaseOperand(&var);  // will dec ref if var.flags & kRefIncrd
  set_type(&field, x->field()->type());
  // fields cannot be undefined
#ifdef NDEBUG
  // perform this optimization in optimized mode only
  clear_flags(&field, kCheckUndef | kCheckNull);
#else
  // check field for undef and null in debug mode (do not clear flags)
#endif

  if (op == floadV) {
    x_ = field;
  } else if (op == fstoreV) {
    StoreVal(&field, &val, true);
  } else if (op == finc64) {
    IncVal(&field, delta());
  } else if (op == floadVu) {
    UniqVal(&field, x->field()->type());
  } else {
    ShouldNotReachHere();
  }
}


void NCodeGen::DoRuntimeGuard(RuntimeGuard* x) {
  Trace t(&tlevel_, "(RuntimeGuard");
  // evaluate the guard condition and trap if false
  NLabel tguard(proc_);
  NLabel fguard(proc_);
  LoadConditional(x->guard(), false, &tguard, &fguard);
  BranchShort(branch_true, &tguard);
  Bind(&fguard);
  Operand trap_info(AM_IMM, x->msg());
  Trap(&trap_info, false, AM_NONE, 0);
  // evaluate the expression
  Bind(&tguard);
  Visit(x->expr());
}


void NCodeGen::IndexSliceNonMap(Expr* var, Expr* beg, Expr* end, int delta,
                  Expr* length_temp, Type* type, FunPtr fun, bool check_err) {
  // This is shared between DoIndex and DoSlice because they are almost
  // identical for the non-map case including identical length temp handling.
  Operand rhs;
  if (delta == 0 && !is_load()) {
    assert(x_.am != AM_NONE);
    rhs = x_;
    x_.Clear();
  } else {
    assert(x_.am == AM_NONE);
  }
  // If beg (index) or end uses "$" and the length needs to be stored in a temp,
  // load the array now and deal with the length.  Else load it later.
  // (Copied from the FSetB code.)
  Operand var_opnd;  // holds loaded array, string, bytes or map
  RegsState arg_regs;
  int num_args = fun.num_args;
  if (length_temp != NULL) {
    ProtectAndLoad(&rhs, var, is_lhs(), &var_opnd);
    assert(is_szl_val(&var_opnd));
    LoadOperand(&var_opnd, RS_CALLEE_SAVED);  // avoid a reg save across Len call
    assert(x_.am == AM_NONE);
    Operand var_clone = var_opnd;
    ReserveRegs(&var_clone);
    clear_flags(&var_opnd, kRefIncrd);  // cannot skip inc ref twice
    InlineLenIntrinsic(&var_clone, var->type());
    Store(length_temp, 0);

    // Preload the original var (and rhs if used) for the index/slice call
    if (rhs.am != AM_NONE)
      PreloadArgs(&var_opnd, num_args - 2, &rhs, num_args - 1,
                  num_args, &arg_regs);
    else
      PreloadArg(&var_opnd, num_args - 1, num_args, &arg_regs);
  } else {
    // Preload rhs (if used) for the index/slice call
    if (rhs.am != AM_NONE)
      PreloadArg(&rhs, num_args - 1, num_args, &arg_regs);
  }

  { FunctionCall fc(this, fun, &arg_regs, type, check_err);
    if (rhs.am != AM_NONE)
      PushVal(&rhs);
    if (length_temp != NULL)
      PushVal(&var_opnd);  // already loaded
    else
      PushExpr(var, is_lhs());
    PushExpr(beg, false);
    if (end != NULL)
      PushExpr(end, false);

    if (delta != 0) {
      assert(delta == -1 || delta == 1);
      Operand delta_imm(AM_IMM, delta);
      delta_imm.size = 1;
      PushOperand(&delta_imm);
    }
  }
}


void NCodeGen::DoIndex(Index* x) {
  Trace t(&tlevel_, "(Index");
  Type* type = x->var()->type();
  bool check_err = false;
  if (type->is_indexable()) {
    Opcode op = IndexedAccess(type, is_load(), is_lhs(), delta());
    FunPtr fun;

    switch (op) {
      case xinc8:
        fun = ChkFunPtr<int (Proc* proc, int8 delta, IntVal* x, BytesVal* b)>(NSupport::XInc8);
        check_err = true;
        break;

      case xincR:
        fun = ChkFunPtr<int (Proc* proc, int8 delta, IntVal* x, StringVal* s)>(NSupport::XIncR);
        check_err = true;
        break;

      case xinc64:
        fun = ChkFunPtr<int (Proc* proc, int8 delta, IntVal* x, ArrayVal* a)>(NSupport::XInc64);
        check_err = true;
        break;

      case xload8:
        fun = ChkFunPtr<Val* (Proc* proc, IntVal* x, BytesVal* b)>(NSupport::XLoad8);
        break;

      case xloadR:
        fun = ChkFunPtr<Val* (Proc* proc, IntVal* x, StringVal* s)>(NSupport::XLoadR);
        break;

      case xloadV:
        fun = ChkFunPtr<Val* (Proc* proc, IntVal* x, ArrayVal* a)>(NSupport::XLoadV);
        break;

      case xloadVu:
        fun = ChkFunPtr<Val* (Proc* proc, IntVal* x, ArrayVal* a)>(NSupport::XLoadVu);
        break;

      case xstore8:
        fun = ChkFunPtr<int (Proc* proc, IntVal* x, BytesVal* b, IntVal* e)>(NSupport::XStore8);
        check_err = true;
        break;

      case xstoreR:
        fun = ChkFunPtr<int (Proc* proc, IntVal* x, StringVal* s, IntVal* e)>(NSupport::XStoreR);
        check_err = true;
        break;

      case xstoreV:
        fun = ChkFunPtr<int (Proc* proc, IntVal* x, ArrayVal* a, Val* e)>(NSupport::XStoreV);
        check_err = true;
        break;

      default:
        ShouldNotReachHere();
    }

    IndexSliceNonMap(x->var(), x->index(), NULL, delta(), x->length_temp(),
            (check_err ? SymbolTable::bad_type() : x->type()), fun, check_err);

    TrapIfUndefOperand(&x_, true);  // pass proc_->trap_info_ to trap handler
    if (check_err)
      ReleaseOperand(&x_);  // result was an error status, not a Val*

  } else if (type->is_map()) {
    Operand rhs;
    if (delta() == 0 && !is_load()) {
      assert(x_.am != AM_NONE);
      rhs = x_;
      x_.Clear();
    } else {
      assert(x_.am == AM_NONE);
    }
    Opcode opkey = MappedKey(type->as_map(), is_load(), is_lhs(), delta(), proc_, &error_count_);
    Opcode opvalue = MappedValue(type->as_map(), is_load(), is_lhs(), delta(), proc_, &error_count_);
    // avoid Unimplemented() - keep regression test failure clean.
    if (opkey == illegal || opvalue == illegal) {
      Operand trap_info(AM_IMM, proc_->PrintString("can't codegen index %T", type));
      Trap(&trap_info, true, AM_NONE, 0);
    } else {
      assert(opkey == mloadV || opkey == minsertV);
      check_err = (opkey == mloadV);

      if (rhs.am != AM_NONE) {
        // no need to use ProtectAndLoad to protect rhs while loading var and
        // index, because we can simply inc ref rhs here, since it will be pushed
        // anyway as arg later
        IncRefOperand(&rhs, RS_ANY);
        set_flags(&rhs, kRefIncrd);  // to suppress the inc ref in PushVal
      }

      // operand var will be used twice, load in callee-saved reg and inc ref it
      // only once; the first helper will not dec ref it (unless it traps)
      Load(x->var(), is_lhs());
      IncRefOperand(&x_, RS_CALLEE_SAVED);
      set_flags(&x_, kRefIncrd);  // to suppress the inc ref in both PushVal
      Operand var = x_;
      x_.Clear();
      Operand var_clone = var;
      ReserveRegs(&var_clone);

      { RegsState arg_regs;
        PreloadArg(&var, 1, 3, &arg_regs);
        ChkFunPtr<Val* (Proc* proc, MapVal* m, Val* key)>
          fun_ptr(opkey == mloadV ? NSupport::MLoadV : NSupport::MInsertV);
        FunctionCall fc(this, fun_ptr, &arg_regs, type, check_err);
        assert(type != NULL && !type->is_void());

        PushExpr(x->index(), false);
        assert(is_ref_incrd(&var));  // already done above
        PushVal(&var);
      }
      // x_ is index
      if (check_err)
        TrapIfUndefOperand(&x_, true);  // NSupport::MLoadV dec ref var if undef

      FunPtr fun;
      Type* result_type = x->type();
      int m_pos = 1;

      switch (opvalue) {
        case minc64:
          fun = ChkFunPtr<void (Proc* proc, int8 delta, MapVal* m, IntVal* index)>(NSupport::MInc64);
          m_pos = 2;
          assert(rhs.am == AM_NONE);
          result_type = NULL;  // x->type() is not NULL or void
          break;

        case mindexV:
          fun = ChkFunPtr<Val* (MapVal* m, IntVal* index)>(NSupport::MIndexV);
          m_pos = 0;
          assert(rhs.am == AM_NONE);
          break;

        case mindexVu:
          fun = ChkFunPtr<Val* (Proc* proc, MapVal* m, IntVal* index)>(NSupport::MIndexVu);
          assert(rhs.am == AM_NONE);
          break;

        case mstoreV:
          fun = ChkFunPtr<void (MapVal* m, IntVal* index, Val* value)>(NSupport::MStoreV);
          assert(rhs.am != AM_NONE);
          result_type = NULL;  // x->type() is not NULL or void
          break;

        default:
          ShouldNotReachHere();
      }

      { RegsState arg_regs;
        if (fun.non_szl_fun == reinterpret_cast<void (*)()>(NSupport::MStoreV))
          PreloadArg(&rhs, 2, 3, &arg_regs);

        PreloadArgs(&var_clone, m_pos, &x_, m_pos + 1, fun.num_args, &arg_regs);
        FunctionCall fc(this, fun, &arg_regs, result_type, check_err);

        if (fun.non_szl_fun == reinterpret_cast<void (*)()>(NSupport::MStoreV))
          PushVal(&rhs);

        PushVal(&x_);
        assert(is_ref_incrd(&var_clone));  // already done above
        PushVal(&var_clone);

        if (delta() != 0) {
          assert(delta() == -1 || delta() == 1);
          Operand delta_imm(AM_IMM, delta());
          delta_imm.size = 1;
          PushOperand(&delta_imm);
        }
      }
    }

  } else {
    // there are no other indexable types
    ShouldNotReachHere();
  }
}


void NCodeGen::DoNew(New* x) {
  Trace t(&tlevel_, "(New");
  Type* type = x->type();
  assert(type->is_allocatable());
  if (type->is_array()) {
    ChkFunPtr<Val* (Proc* proc, ArrayType* atype, IntVal* length, Val* init)>
      fun_ptr(NSupport::NewA);
    FunctionCall fc(this, fun_ptr, NULL, type, true);

    PushExpr(x->init(), false);
    PushExpr(x->length(), false);

    Operand type_imm(AM_IMM, type);
    PushOperand(&type_imm);

  } else if (type->is_bytes()) {
    ChkFunPtr<Val* (Proc* proc, IntVal* length, IntVal* init)>
      fun_ptr(NSupport::NewB);
    FunctionCall fc(this, fun_ptr, NULL, type, true);

    PushExpr(x->init(), false);
    PushExpr(x->length(), false);

  } else if (type->is_map()) {
    ChkFunPtr<Val* (Proc* proc, MapType* mtype, IntVal* occupancy)>
      fun_ptr(NSupport::NewM);
    FunctionCall fc(this, fun_ptr, NULL, type, true);

    PushExpr(x->length(), false);

    Operand type_imm(AM_IMM, type);
    PushOperand(&type_imm);

  } else if (type->is_string()) {
    ChkFunPtr<Val* (Proc* proc, IntVal* nrunes, IntVal* init)>
      fun_ptr(NSupport::NewStr);
    FunctionCall fc(this, fun_ptr, NULL, type, true);

    PushExpr(x->init(), false);
    PushExpr(x->length(), false);

  } else {
    // there are no other allocatable types
    ShouldNotReachHere();
  }
  TrapIfInfo(false);
}


void NCodeGen::DoRegex(Regex* x) {
  Trace t(&tlevel_, "(Regex");
  assert(x_.am == AM_NONE);
  const char* pat = RegexPattern(x, proc_, &error_count_);
  // we still set x_ to pat in case of error (pat == "") to make ncodegen happy
  DoLiteral(Literal::NewString(proc_, x->file_line(), NULL, pat));
}


void NCodeGen::DoSaw(Saw* x) {
  Trace t(&tlevel_, "(Saw");

  // Generate a single call to NSupport::Saw with a va_list of all args
  // needed for the multiple calls to Intrinsics::Saw to be issued by
  // NSupport::Saw. Each call to Intrinsics::Saw leaves 2 Val pointers (result
  // array and string being sawn) on the interpreter stack, which makes it
  // difficult to implement as one native call each. The order in which the args
  // are pushed on the native stack is the reverse order in which they will be
  // pushed on the interpreter stack by NSupport::Saw.
  // Remember that all Val* arguments must be pushed before all non-Sawzall
  // arguments for proper trap handling. See frame.h. Therefore, the va_list
  // will consist of num_vars pairs of non-Sawzall values (regex_count and
  // var_addr) used by the support call only, followed by Sawzall values passed
  // to the intrinsic call.

  const int len = x->args()->length();
  assert(len > 0);
  // Calculate the number of args in the va_list:
  // let's define num_REST as the number of REST flags in x->flags().
  // the list consists of num_REST pairs of {regex_count, var_addr}, one str,
  // one count, and len-num_REST pairs of {regex, flag}
  // if the last flag is not REST, we add a {regex_count, null} pair.
  const int num_args = 2 + 2*len + (x->flags()->at(len - 1) == Saw::REST ? 0 : 2);

  ChkFunPtr<Val* (Proc* proc, void** cache, int num_vars, int num_args, ...)>
      fun_ptr(NSupport::Saw, num_args);
  { FunctionCall fc(this, fun_ptr, NULL, x->type(), true);
    int arg0;
    // Push the Sawzall args first, i.e. flags and regexes
    for (int argn = len; argn > 0; argn = arg0) {
      if (x->flags()->at(argn - 1) == Saw::REST)
        argn--;  // do not count the rest argument as a regex in regex_count

      for (arg0 = argn; arg0 > 0 && x->flags()->at(arg0 - 1) != Saw::REST; --arg0)
          ;
      // arg0 == 0 || x->flags()[arg0 - 1] == Saw::REST
      // args()->at(argn - 1) is the last regex we are using in this iteration
      const int regex_count = argn - arg0;

      // invoke saw if necessary
      if (regex_count > 0) {
        // we have at least one regex
        // load flag arguments
        for (int i = arg0; i < argn; ++i) {
          int flag = x->flags()->at(i);
          assert(flag != Saw::REST);
          Operand flag_imm(AM_IMM, Factory::NewInt(proc_, flag));
          PushOperand(&flag_imm);  // ref count incremented already, smi anyway
        }
        // load regex arguments
        PushExprs(x->args(), arg0, argn - arg0);
      }
    }

    PushExpr(x->count(), false);
    PushExpr(x->str(), false);

    // Now push the non-Sawzall args
    int num_vars = 0;
    for (int argn = len; argn > 0; argn = arg0) {
      if (x->flags()->at(argn - 1) == Saw::REST) {
        Load(x->args()->at(argn - 1), false);  // rest only supports a simple var
        // delayed emission of code will provide an operand pointing to the var
        PushAddr(&x_);
        argn--;  // do not count the rest argument as a regex in regex_count
      } else {
        Operand null(AM_IMM, 0);
        PushOperand(&null);
      }
      for (arg0 = argn; arg0 > 0 && x->flags()->at(arg0 - 1) != Saw::REST; --arg0)
          ;
      // arg0 == 0 || x->flags()[arg0 - 1] == Saw::REST
      // args()->at(argn - 1) is the last regex we are using in this iteration
      const int regex_count = argn - arg0;
      Operand regex_count_imm(AM_IMM, regex_count);
      PushOperand(&regex_count_imm);
      num_vars++;
    }

    Operand num_args_imm(AM_IMM, num_args);
    PushOperand(&num_args_imm);

    Operand num_vars_imm(AM_IMM, num_vars);
    PushOperand(&num_vars_imm);

    // allocate space to hold the cached pattern, better not store it in code
    // use ALLOC macro for non-reference-counted object, see memory.h
    void** cache = ALLOC(proc_, void*, sizeof(void*));
    *cache = NULL;  // init
    *reinterpret_cast<bool*>(cache) = x->static_args();
    Operand cache_imm(AM_IMM, cache);
    PushOperand(&cache_imm);
  }
  TrapIfInfo(false);
  // now we have the resulting array of string as the current operand
}


void NCodeGen::DoStatExpr(StatExpr* x) {
  Trace t(&tlevel_, "(StatExpr");

  // Save any live caller-saved registers.
  RegsState saved_regs(regs_);
  saved_regs.ReleaseRegs(~RS_CALLER_SAVED & RS_ANY);  // keep only caller-saved
  asm_.PushRegs(saved_regs.live());
  regs_.ReleaseRegs(&saved_regs);  // saved regs are not live anymore

  x->set_exit(NCodeGen::NewLabel(proc_));  // target for result statement
  // The body of a statement expression may contain a static variable x.
  // If the statement expression itself is used in an initialization
  // expression for another static variable y, initialization code for
  // x is generated twice if we are not careful: once when encountering x
  // and once when encountering y during static variable initialization.
  // Avoid problem by resetting the do_statics_ flag temporarily.
  bool do_statics_saved = do_statics_;  // save do_statics_
  do_statics_ = false;  // ignore static variables in x->body()
  Execute(x->body());
  do_statics_ = do_statics_saved;  // restore do_statics_
  // Generate a run-time error if no result statement is executed.
  FileLine* fl = x->file_line();
  szl_string msg;
  msg = proc_->PrintString("missing result in ?{} that begins at %L", fl);
  Operand trap_info(AM_IMM, msg);
  Trap(&trap_info, true, AM_NONE, 0);
  Bind(x->exit());

  // Restore saved registers.
  // If the mechanism for result statements is ever changed to use a register
  // instead of a temporary variable, we have to account for that here.
  // See ReleaseCallArea().
  assert((regs_.live() & saved_regs.live()) == 0);  // restore does not clobber
  asm_.PopRegs(saved_regs.live());
  regs_.ReserveRegs(&saved_regs);  // restored regs are live again

  Load(x->var(), false);
}


void NCodeGen::DoSlice(Slice* x) {
  Trace tr(&tlevel_, "(Slice");
  if (is_lhs() && is_load()) {
    Error("can't handle sliced store of arrays yet");
    return;  // do not continue with potentially bad data
  }
  FunPtr fun;
  Type* type;
  bool check_err;
  if (!is_load()) {
    type = NULL;
    check_err = true;
    fun = ChkFunPtr<void (Proc* proc, IntVal* end, IntVal* beg,
                          IndexableVal* a, Val* x)>(NSupport::SStoreV);
  } else {
    type = x->type();
    check_err = false;
    if (type->is_array())
      fun = ChkFunPtr<Val* (Proc* proc, IntVal* end, IntVal* beg,
                            ArrayVal* a)>(NSupport::SLoadV);
    else if (type->is_bytes())
      fun = ChkFunPtr<Val* (Proc* proc, IntVal* end,
                            IntVal* beg, BytesVal* b)>(NSupport::SLoad8);
    else if (type->is_string())
      fun = ChkFunPtr<Val* (Proc* proc, IntVal* end, IntVal* beg,
                            StringVal* s)>(NSupport::SLoadR);
    else
      ShouldNotReachHere();
  }

  IndexSliceNonMap(x->var(), x->beg(), x->end(), 0, x->length_temp(), type, fun,
                   check_err);
  // Note that if we were to use Slice::CanCauseTrap() here,
  // we would have to pass (!is_load()) as an argument.
  if (check_err)
    TrapIfInfo(false);
}


void NCodeGen::DoLiteral(Literal* x) {
  Trace(&tlevel_, "Literal %n", source(), x);
  assert(x_.am == AM_NONE);
  Operand literal(AM_IMM, x->val());
  set_type(&literal, x->type());
  // no undef constant in szl, so no undef check or null check necessary:
  // do not set kCheckUndef or kCheckNull flags
  x_ = literal;
}


void NCodeGen::DoVariable(Variable* x) {
  Trace(&tlevel_, "Variable %n", source(), x);
  assert(delta() == 0 || !x->is_static());  // ++//-- only legal for locals
  AddrMod bp_reg = GetBP(x->level(), RS_ANY);  // bp_reg is reserved
  Operand var(AM_BASED + bp_reg, kPtrSize, x->offset());
  set_type(&var, x->type());
  Opcode op = VariableAccess(x->type(), is_load(), is_lhs(), delta());
  if (op == storeV) {
    // operand x_ is the value to store
    assert(x_.am != AM_NONE);
    StoreVal(&var, &x_, true);
  } else {
    assert(x_.am == AM_NONE);
    if (x->CanCauseTrap(false)) {
      set_flags(&var, kCheckUndef | kCheckNull);
      set_var(&var, x->var_decl());  // for undef variable traps
    }
    if (op == loadV) {
      // we postpone the def check for a simple loadV
      x_ = var;
    } else if (delta() != 0) {
      assert(op == inc64);
      IncVal(&var, delta());
    } else {
      assert(op == loadVu);
      UniqVal(&var, x->type());
    }
  }
}


void NCodeGen::DoTempVariable(TempVariable* x) {
  if (x->init() != NULL && !x->initialized()) {
    assert(is_load());
    x->set_initialized();  // must set before calling Store()
    Load(x->init(), is_lhs());
    Operand init_clone = x_;
    clear_flags(&init_clone, kRefIncrd);  // have not counted this ref yet
    ReserveRegs(&init_clone);
    Store(x, 0);
    x_ = init_clone;
  } else {
    DoVariable(x);
  }
}


// ----------------------------------------------------------------------------
// Visitor functionality
// statements

void NCodeGen::DoAssignment(Assignment* x) {
  Trace t(&tlevel_, "(Assignment");
  NLabel exit(proc_);
  { NTrapHandler handler(this, &exit, UndefVar(x->lvalue())->var_decl(), false, x);
    if (x->is_dead()) {
      // evaluate and discard RHS and non-dead part of LHS for side effects
      Load(x->rvalue(), false);
      DiscardResult(x->rvalue()->type());
      LoadLHS(x->selector_var());
      DiscardResult(x->selector_var()->type());
    } else {
      Load(x->rvalue(), false);
      Store(x->lvalue(), 0);
    }
  }
  Bind(&exit);
}


void NCodeGen::DoBlock(Block* x) {
  Trace t(&tlevel_, "(Block");
  for (int i = 0; i < x->length(); i++)
    Execute(x->at(i));
}


void NCodeGen::DoBreak(Break* x) {
  Trace t(&tlevel_, "(Break");
  Branch(branch, x->stat()->exit());
}


void NCodeGen::DoContinue(Continue* x) {
  Trace t(&tlevel_, "(Continue");
  Branch(branch, x->loop()->cont());
}


void NCodeGen::DoTypeDecl(TypeDecl* x) {
  // nothing to do
}


void NCodeGen::DoVarDecl(VarDecl* x) {
  Trace t(&tlevel_, "(VarDecl %s", x->name());
  // either do all statics or all locals
  if (x->is_static() == do_statics()) {
    // determine initial variable value
    if (x->type()->is_output()) {
      assert(do_statics());  // output variables are static
      // initialize output variable
      TableInfo* t = TableInfo::New(proc_, x->name(), x->type()->as_output());
      tables_->Append(t);
      // we allow arbitrary expressions as table parameters, so
      // we have to catch and die in cases when param evaluation
      // results in undefined values or values out of range
      NLabel exit(proc_);
      { NTrapHandler handler(this, &exit, NULL, false, x);
        ChkFunPtr<void (Proc* proc, Frame* bp, int var_index,
                        int tab_index, IntVal* param)> fun_ptr(NSupport::OpenO);
        FunctionCall fc(this, fun_ptr, NULL, NULL, true);

        Expr* param = x->type()->as_output()->param();
        if (param != NULL) {
          PushExpr(param, false);
        } else {
          Operand dummy(AM_IMM, TaggedInts::MakeVal(-1));
          PushOperand(&dummy);
        }

        Operand tab_idx(AM_IMM, tables_->length() - 1);
        PushOperand(&tab_idx);

        Operand var_idx(AM_IMM, var_index(x->offset()));
        PushOperand(&var_idx);

        PushBP(0);
      }
      TrapIfInfo(true);  // emitter installation errors are fatal
      Bind(&exit);
    } else if (x->init() != NULL) {
      NLabel exit(proc_);
      { NTrapHandler handler(this, &exit, (do_statics() ? NULL : x), !do_statics(), x->init());
        Load(x->init(), false);
        StoreVarDecl(x);
      }
      Bind(&exit);

    } else {
      // nothing to do (all variables are NULLed out in the beginning)
    }
  }
}


// todo the original evaluation order is not preserved, because runtime
// helpers using va_list are used; inline pushes to interpreter stack to fix.
void NCodeGen::DoEmit(Emit* x) {
  Trace t(&tlevel_, "(Emit");
  List<VarDecl*>* index_decls = x->index_decls();
  List<Expr*>* indices = x->indices();
  int num_index_decls = index_decls->length();
  int num_args = 1;  // value
  if (x->weight() != NULL)
    num_args++;  // optional weight

  if (x->index_format() != NULL)
    // indices are formatted into one string
    num_args++;
  else
    num_args += num_index_decls;

  NLabel exit(proc_);
  { NTrapHandler handler(this, &exit, NULL, false, x);
    if (x->index_format() != NULL) {
      // we have an index format; assign the indices to the index variables
      for (int i = 0; i < num_index_decls; i++) {
        Load(indices->at(i), false);
        StoreVarDecl(index_decls->at(i));
      }
    }

    if (x->elem_format() != NULL) {
      // we have an element format; assign the value to the element variable
      Load(x->value(), false);
      StoreVarDecl(x->elem_decl());
    }

    // call NSupport::Emit and check returned error message
    ChkFunPtr<void (Proc* proc, int num_args, Val* var, ...)>
      fun_ptr(NSupport::Emit, num_args);
    FunctionCall fc(this, fun_ptr, NULL, NULL, true);
    if (x->index_format() != NULL) {
      // evaluate index format() and push on stack
      PushExpr(x->index_format(), false);
    } else {
      // push indices on stack, if any
      // the last va_arg in NSupport::Emit must be the first index
      for (int i = 0; i < num_index_decls; i++)
        PushExpr(indices->at(i), false);
    }
    if (x->elem_format() != NULL) {
      // evaluate element format() and push on stack
      PushExpr(x->elem_format(), false);
    } else {
      // push value on stack
      PushExpr(x->value(), false);
    }
    // push 'weight' on stack, if any
    if (x->weight() != NULL)
      PushExpr(x->weight(), false);

    PushExpr(x->output(), false);

    Operand num_args_imm(AM_IMM, num_args);
    PushOperand(&num_args_imm);
  }
  TrapIfInfo(true);  // emit errors are always fatal
  Bind(&exit);
}


void NCodeGen::DoEmpty(Empty* x) {
  // nothing to do
}


void NCodeGen::DoExprStat(ExprStat* x) {
  Trace t(&tlevel_, "(ExprStat");
  NLabel exit(proc_);
  { NTrapHandler handler(this, &exit, NULL, false, x->expr());
    Expr* e = x->expr();
    Load(e, false);
    DiscardResult(e->type());
  }
  Bind(&exit);
}


void NCodeGen::DoIf(If* x) {
  Trace t(&tlevel_, "(If");
  // generate different code depending on which
  // parts of the if statement are present or not
  bool has_then = x->then_part()->AsEmpty() == NULL;
  bool has_else = x->else_part()->AsEmpty() == NULL;

  NLabel exit(proc_);
  if (has_then && has_else) {
    NLabel then(proc_);
    NLabel else_(proc_);
    // if (cond)
    { NTrapHandler handler(this, &exit, NULL, false, x->cond());
      LoadConditional(x->cond(), false, &then, &else_);
      Branch(branch_false, &else_);
    }
    // then
    Bind(&then);
    Execute(x->then_part());
    Branch(branch, &exit);
    // else
    Bind(&else_);
    Execute(x->else_part());

  } else if (has_then) {
    assert(!has_else);
    NLabel then(proc_);
    // if (cond)
    { NTrapHandler handler(this, &exit, NULL, false, x->cond());
      LoadConditional(x->cond(), false, &then, &exit);
      Branch(branch_false, &exit);
    }
    // then
    Bind(&then);
    Execute(x->then_part());

  } else if (has_else) {
    assert(!has_then);
    NLabel else_(proc_);
    // if (!cond)
    { NTrapHandler handler(this, &exit, NULL, false, x->cond());
      LoadConditional(x->cond(), false, &exit, &else_);
      Branch(branch_true, &exit);
    }
    // else
    Bind(&else_);
    Execute(x->else_part());

  } else {
    assert(!has_then && !has_else);
    // if (cond)
    { NTrapHandler handler(this, &exit, NULL, false, x->cond());
      LoadConditional(x->cond(), false, &exit, &exit);
      ReleaseOperand(&x_);
    }
  }

  // end
  Bind(&exit);
}


void NCodeGen::DoIncrement(Increment* x) {
  Trace t(&tlevel_, "(Increment");
  NLabel continuation(proc_);
  assert(x->delta() == 1 || x->delta() == -1);
  { NTrapHandler handler(this, &continuation, UndefVar(x->lvalue())->var_decl(),
                         false, x->lvalue());
    Store(x->lvalue(), x->delta());
  }
  Bind(&continuation);
}


void NCodeGen::DoResult(Result* x) {
  Trace t(&tlevel_, "(Result");
  NLabel exit(proc_);
  Variable* tempvar = x->statexpr()->var();
  { NTrapHandler handler(this, &exit, UndefVar(tempvar)->var_decl(), false,
                         x->expr());
    Load(x->expr(), false);
    Store(tempvar, 0);
  }
  Bind(&exit);
  Branch(branch, x->statexpr()->exit());
}


void NCodeGen::DoReturn(Return* x) {
  Trace t(&tlevel_, "(Return");
  if (x->has_result()) {
    NTrapHandler handler(this, global_trap_handler_, NULL, true, x->result());
    Load(x->result(), false);
    IncRefOperand(&x_, RS_EAX);
    LoadOperand(&x_, RS_EAX);
    ReleaseOperand(&x_);
    Branch(branch, return_);
    // remember this trap site and that it was a return
    if (x->result()->CanTrap()) {
      assert(current_trap_range_ != NULL);
      current_trap_range_->AddTrap(emit_offset() - 1, NULL);
    }
  } else {
    Branch(branch, return_);
  }

  // if return is inlined later, do not forget to call asm_.set_dead_code(true);
  // since code following a return is dead
}


void NCodeGen::DoSwitch(Switch* x) {
  Trace t(&tlevel_, "(Switch");
  x->set_exit(NCodeGen::NewLabel(proc_));
  // switch (tag)
  NTrapHandler handler(this, x->exit(), NULL, false, x->tag());
  Load(x->tag(), false);
  // keep the tag in a callee saved register, so that it is preserved through
  // label loading and comparing, release it before executing the matching case
  LoadOperand(&x_, RS_CALLEE_SAVED);
  Operand saved_tag = x_;
  x_.Clear();
  // handle each case
  Type* tag_type = x->tag()->type();
  List<Case*>* cases = x->cases();
  for (int i = 0; i < cases->length(); i++) {
    NLabel next_case(proc_);
    NLabel case_stat(proc_);
    Case* case_ = cases->at(i);
    // handle each label
    List<Expr*>* labels = case_->labels();
    for (int i = 0; i < labels->length(); i++) {
      Expr* label_expr = labels->at(i);
      bool last = i == labels->length() - 1;
      NLabel next_label(proc_);
      { NTrapHandler handler(this, last ? &next_case : &next_label, NULL,
                             false, label_expr);
        Operand tag = saved_tag;
        // saved_tag.flags & kRefIncrd is used when releasing saved_tag
        clear_flags(&tag, kRefIncrd);
        ReserveRegs(&tag);  // do not release tag register yet
        Operand label;
        ProtectAndLoad(&tag, label_expr, false, &label);
        Compare(&tag, &label, tag_type);
        if (last)
          Branch(branch_false, &next_case);
        else
          Branch(branch_true, &case_stat);
      }
      if (!last)
        Bind(&next_label);
    }
    Bind(&case_stat);
    Operand tag = saved_tag;
    ReleaseOperand(&tag);  // will not dec ref if is_ref_incrd(&saved_tag)
    // tag register was released and can be used in case code
    NTrapHandler handler(this, x->exit(), NULL, false, labels->at(0));
    Execute(case_->stat());
    Branch(branch, x->exit());
    Bind(&next_case);
    // reserve tag register again for next case
    ReserveRegs(&saved_tag);
  }
  // handle default
  ReleaseOperand(&saved_tag);  // will not dec ref if is_ref_incrd(&saved_tag)
  Execute(x->default_case());
  // end
  Bind(x->exit());
}


void NCodeGen::DoWhen(When* x) {
  Trace t(&tlevel_, "(When");
  if (FLAGS_v > 0)
    F.print("rewrite of when:\n%1N\n", x->rewritten());
  Visit(x->rewritten());
}


void NCodeGen::DoLoop(Loop* x) {
  Trace t(&tlevel_, "(Loop");
  NLabel entry(proc_);
  NLabel loop(proc_);
  x->set_cont(NCodeGen::NewLabel(proc_));  // for continue statement
  x->set_exit(NCodeGen::NewLabel(proc_));  // for break statement
  // init
  if (x->before() != NULL) {
    assert(x->sym() == FOR);
    Execute(x->before());
  }
  if (x->sym() != DO)
    Branch(branch, &entry);
  // body
  Bind(&loop);
  Execute(x->body());
  Bind(x->cont());
  if (x->after() != NULL) {
    assert(x->sym() == FOR);
    Execute(x->after());
  }
  // cond
  Bind(&entry);
  BoolVal* cond = NULL;
  if (x->cond() != NULL)
    cond = x->cond()->as_bool();
  if ((x->sym() == FOR && x->cond() == NULL) ||
      (x->sym() != FOR && cond != NULL && cond->val())) {
    // condition always true
    Branch(branch, &loop);
  } else {
    NTrapHandler handler(this, x->exit(), NULL, false, x->cond());
    LoadConditional(x->cond(), false, &loop, x->exit());
    Branch(branch_true, &loop);
  }
  // end
  Bind(x->exit());
}

}  // namespace sawzall
