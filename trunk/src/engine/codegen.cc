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
#include "public/commandlineflags.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/convop.h"
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
#include "engine/codegen.h"
#include "public/emitterinterface.h"
#include "engine/outputter.h"
#include "engine/compiler.h"
#include "engine/intrinsic.h"
#include "engine/codegenutils.h"


DEFINE_bool(eliminate_dead_code, true, "enable dead code elimination");
DEFINE_bool(szl_bb_count, false, "generate szl basic block execution counts");


namespace sawzall {


// ----------------------------------------------------------------------------
// Labels
//
// BLabels represent branch and call targets during byte code generation.

class BLabel: public Label {
 public:
  BLabel(Proc* proc)
    : forward_(proc),
      other_(proc),
      target_(-1),
      stack_height_(0) {
  }
  virtual ~BLabel()  { assert(!is_linked()); }

  // testers
  bool is_bound() const  { return target_ >= 0; }
  bool is_linked() const  { return forward_.length() > 0 || other_.length() > 0; }

  // code generation
  void bind_to(int pos, int stack_height, Instr* base);  // bind BLabel to position pos (relative to base)
  void add_dep(int* dep, int stack_height);  // register another dependency
  Code::pcoff offset(int pos, int stack_height);  // (branch) offset to BLabel from current position pos

 private:
  // branch offsets
  static const int kOffsetSize = sizeof(Code::pcoff);

  // Invariant: (forward_.length() > 0) != (target != NULL)
  List<int> forward_;  // list of forward branch positions
  List<int*> other_;  // list of other dependencies (usually trap targets)
  int target_;  // branch destination after bind_to()
  int stack_height_;  // the stack height for this control flow
};


void BLabel::bind_to(int pos, int stack_height, Instr* base) {
  assert(pos >= 0);
  assert(!is_linked() || stack_height == stack_height_);
  // resolve forward references
  for (int i = forward_.length(); i-- > 0; ) {
    int f = forward_[i];
    int offs = pos - f - kOffsetSize;
    CHECK(offs == static_cast<Code::pcoff>(offs));  // make sure offset fits into code
    *reinterpret_cast<Code::pcoff*>(&base[f]) = offs;
  }
  forward_.Clear();
  // resolve other dependencies
  for (int i = other_.length(); i--; )
    *other_[i] = pos;
  other_.Clear();
  // bind label
  target_ = pos;
  stack_height_ = stack_height;
}


void BLabel::add_dep(int* dep, int stack_height){
  assert(dep != NULL);
  assert(!is_linked() || stack_height == stack_height_);
  other_.Append(dep);
  stack_height_ = stack_height;
}


Code::pcoff BLabel::offset(int pos, int stack_height) {
  assert(pos >= 0);
  if (is_bound()) {
    // the label's position is known and we can compute the effective offset
    assert(stack_height == stack_height_);
    int offs = target_ - pos - kOffsetSize;
    CHECK(offs == static_cast<Code::pcoff>(offs));  // make sure offset fits into code
    return static_cast<Code::pcoff>(offs);
  } else {
    // the label's position is unknown and we need to keep a (forward) reference
    assert(!is_linked() || stack_height == stack_height_);
    forward_.Append(pos);
    stack_height_ = stack_height;
    return 0;
  }
}


// ----------------------------------------------------------------------------
// StackMark
//
// StackMark is a (C++) stack-allocated class used to maintain stack
// height computation. Upon construction, StackMark is remembering
// the current stack height maintained by the code generator. Upon
// destruction (of StackMark) it resets the stack height to the original
// value. This is useful for regions where the compiler's stack height
// computation is incomplete but where we know the stack height at
// the end of the region.

class StackMark {
 public:
  StackMark(CodeGen* cgen) : cgen_(cgen), stack_height_(cgen->stack_height_)  {}
  ~StackMark()  { cgen_->SetStack(stack_height_); }

 private:
  CodeGen* cgen_;
  int stack_height_;
};


// ----------------------------------------------------------------------------
// Trap support

// TrapHandler is a stack-allocated class that takes care of collecting
// TrapRanges during code generation. The trap range is determined
// by when the TrapHandler is constructed (begin) and destroyed (end).

class TrapHandler{
 public:
  TrapHandler(
    CodeGen* cgen,  // the code generator
    Label* target,  // the target label
    VarDecl* var,   // the decl of the root var of the expression that may trap
    bool is_silent,  // true, if the trap is silent even w/o --ignore_undefs
    Node* x  // the corresponding node (for debugging only)
  );
  ~TrapHandler();

 private:
  CodeGen* cgen_;
  TrapDesc* desc_;
};


TrapHandler::TrapHandler(CodeGen* cgen, Label* target, VarDecl* var, bool is_silent, Node* x)
  : cgen_(cgen),
    desc_(NULL) {
  // during initialization, all traps except in def() are fatal
  if (cgen->do_statics() && !is_silent)
    target = cgen->global_trap_handler_;  // override with global handler
  // in debug mode, verify stack height
#ifndef NDEBUG
  cgen->emit_op(verify_sp);
  cgen->emit_int32(cgen->stack_height_);
#endif
  if (x->CanTrap()) {
    // setup a new trap descriptor - we *must* do it in the constructor
    // of TrapHandler (as opposed to the desctructor) because it must
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
                    cgen->stack_height_, 0, var, index, delta, is_silent,
                    cgen->proc_->PrintString("%L: %n",
                                             x->file_line(),
                                             cgen->source(), x),
                    cgen_->current_trap_range_);
    down_cast<BLabel*>(target)->add_dep(&(desc_->target_), cgen->stack_height_);  // register target dependency
    // setup new current trap range (the old range is stored desc_)
    cgen_->current_trap_range_ = desc_;
    // we do not rely on a particular order of the trap ranges
    // => collect them now since it's convenient
    cgen_->trap_ranges_->Append(desc_);
  }
}


TrapHandler::~TrapHandler() {
  if (desc_ != NULL) {
    // stack heights at the begin and end of a trap range must match
    assert(desc_->stack_height() == cgen_->stack_height_);
    // at this point we know the entire code range
    // => complete the setup of the trap desc
    desc_->end_ = cgen_->emit_offset();
    // restore previous super trap range
    cgen_->current_trap_range_ = desc_->super();
  }
}


// ----------------------------------------------------------------------------
// Implementation of CodeGen


const int CodeGen::kNumElems = 4096;


CodeGen::CodeGen(Proc* proc, const char* source, bool debug)
    : proc_(proc),
      source_(source),
      debug_(debug),
      error_count_(0),
      tlevel_("codegen"),

      // setup code buffer
      // (allocated and grown on demand)
      code_buffer_(NULL),
      code_limit_(NULL),
      emit_pos_(NULL),
      dead_code_(false),

      // setup remaining state
      max_stack_height_(0),
      stack_height_(0),
      do_statics_(false),
      tables_(NULL),
      current_trap_range_(NULL),
      trap_ranges_(List<TrapDesc*>::New(proc)),
      line_num_info_(List<Node*>::New(proc)),
      function_(NULL),
      emit_scope_(NULL),
      emit_var_(NULL),
      state_(NULL),
      cc_set_(false),
      global_trap_handler_(NULL) {

  SetStack(0);
  static BCodeGenState state;  // states are read-only
  state_ = &state;
  reset_emit_scope();
}


CodeGen::~CodeGen() {
  delete[] code_buffer_;  // allocated via MakeSpace()
}


void CodeGen::Error(const char* error_msg) {
  assert(error_msg != NULL);
  fprintf(stderr, "szl: error: %s\n", error_msg);
  error_count_++;
}


size_t CodeGen::AllocateStaticOffsets(SymbolTable* symbol_table) {
  const size_t statics_size = Frame::kStaticStartOffset +
    ComputeStaticOffsets(symbol_table->statics(), Frame::kStaticStartOffset, false);
  // no user parameters passed to initialization code (init)
  const size_t params_size =
    ComputeStaticOffsets(symbol_table->statics(), statics_size, true);
  assert(params_size == 0);
  return statics_size;
}


void CodeGen::AllocateFrameOffsets(Function* fun) {
  const size_t locals_size = Frame::kLocalStartOffset +
    ComputeLocalOffsets(fun->locals(), Frame::kLocalStartOffset, false, true);
  const size_t params_size =
    ComputeLocalOffsets(fun->locals(), locals_size, true, true);
  fun->set_locals_size(locals_size);
  fun->set_params_size(params_size);
  fun->set_frame_size(locals_size + params_size);
}


void CodeGen::GenerateInitializers(SymbolTable* symbol_table,
                                   OutputTables* tables, size_t statics_size) {
  CHECK(error_count_ == 0) << ": code generator in error state";
  dead_code_ = false;
  max_stack_height_ = 0;
  SetStack(0);
  do_statics_ = true;
  tables_ = tables;
  function_ = NULL;

  // generate code
  assert(emit_offset() % CodeDesc::kAlignment == 0);
  global_trap_handler_ = new BLabel(proc_);  // explicitly deallocated

  Comment("push global frame");
  // compute the number of slots required for static vars
  size_t frame_size = Align(statics_size, sizeof(Val*));
  emit_op(enter);
  emit_int32(frame_size / sizeof(Val*));
  const int enter_offset = emit_offset();
  emit_int32(0);  // fix at the end when we know the max stack height

  Comment("initialize statics");
  Statics* statics = symbol_table->statics();
  for (int i = 0; i < statics->length(); i++)
    Execute(statics->at(i));

  Comment("terminate (return & leave frame alone)");
  emit_op(terminate);

  // handle initialization failure
  // (only generate this code if needed)
  if (global_trap_handler_->is_linked()) {
    Bind(global_trap_handler_);
    Comment("handle initialization failure");
    emit_op(stop);
    emit_ptr("initialization failed");
  }
  delete global_trap_handler_;
  global_trap_handler_ = NULL;

  // make sure emit_offset is aligned for the next function
  align_emit_offset();

  // we're at the end of the code for this activation
  // frame => fix code generation for 'enter'
  set_int32_at(enter_offset, max_stack_height_);

  // we must not have any open trap ranges
  assert(current_trap_range_ == NULL);
}


void CodeGen::GenerateFunction(Statics* statics,
                               Function* fun,
                               bool leave_unreturned) {
  CHECK(error_count_ == 0) << ": code generator in error state";
  dead_code_ = false;
  max_stack_height_ = 0;
  SetStack(0);
  do_statics_ = false;
  tables_ = NULL;
  function_ = fun;

  // the function entry is only used at runtime to initialize a closure
  // it is therefore not too late to create the entry label here, since all
  // functions are compiled before execution starts
  // the compilation of a function call requires the closure offset,
  // which is allocated before any function is compiled; or the entry itself,
  // which is patched into the calli instruction
  if (fun->entry() == NULL)
    fun->set_entry(CodeGen::NewLabel(proc_));

  // generate code
  assert(emit_offset() % CodeDesc::kAlignment == 0);
  global_trap_handler_ = new BLabel(proc_);  // explicitly deallocated

  // set function entry point
  Bind(fun->entry());
  Comment(proc_->PrintString("function %s", fun->name()));

  Comment("push stack frame");
  emit_op(enter);
  emit_int32(fun->locals_size() / sizeof(Val*));
  const int enter_offset = emit_offset();
  emit_int32(0);  // fix at the end when we know the max stack height

  Comment("function body");
  Execute(fun->body());

  Comment("function end");
  if (fun->ftype()->has_result()) {
    CHECK(!leave_unreturned) << "cannot leave a function with a return hanging";
    emit_op(stop);
    // Missing return.  Create a position for the start of the function.
    // (We do not have a line count and so cannot compute the line number of
    // the end of the function without counting newlines.)
    FileLine* fl = fun->file_line();
    szl_string msg;
    if (fun->name() == NULL) {
      msg = proc_->PrintString(
          "missing return in anonymous function that begins at %L", fl);
    } else {
      msg = proc_->PrintString(
          "missing return in function %s, which begins at %L", fun->name(), fl);
    }
    emit_ptr(msg);
  } else if (leave_unreturned) {
    // Leave this function "hanging", with its stack frame still
    // around, so that additional code (e.g. calls) can be executed in
    // the context of this function's stack frame.
    emit_op(terminate);
  } else {
    emit_op(ret);
    emit_int16(fun->frame_size() / sizeof(Val*));
  }

  // handle undefined results
  // (only generate this code if needed)
  if (global_trap_handler_->is_linked()) {
    Bind(global_trap_handler_);
    Comment("handle undefined results");
    emit_op(retU);  // doesn't pop locals
  }
  delete global_trap_handler_;
  global_trap_handler_ = NULL;

  // make sure emit_offset is aligned for the next function
  align_emit_offset();

  if (leave_unreturned) {
    // If we're leaving this function open, make sure there's room to
    // push arguments to any calls we want to run in its stack frame.
    // We assume that 20 is a reasonable upper bound on the number of
    // arguments we might see.
    max_stack_height_ = MaxInt(max_stack_height_, 20);
  }

  // we're at the end of the code for this activation
  // frame => fix code generation for 'enter'
  set_int32_at(enter_offset, max_stack_height_);

  // we must not have any open trap ranges
  assert(current_trap_range_ == NULL);
}


// ----------------------------------------------------------------------------
// Code emission

void CodeGen::SetStack(int height) {
  assert(height >= 0);
  stack_height_ = height;
  if (height > max_stack_height_)
    max_stack_height_ = height;
}


void CodeGen::MakeSpace() {
  assert(sizeof(Instr) == 1);  // otherwise fix the code below
  assert(emit_pos_ >= code_limit_);  // otherwise should not call this
  // code buffer too small => double size
  Instr* old_buffer = code_buffer_;
  size_t buffer_size = 2 * (code_limit_ - code_buffer_);
  if (buffer_size == 0)
    buffer_size = 32 * 1024;  // adjust as appropriate
  // add some extra space (+32): simplifies the space check
  // in EMIT_PROLOGUE and permits at least one emission
  code_buffer_ = new Instr[buffer_size + 32];  // explicitly deallocated
  CHECK(code_buffer_ != NULL) << "failed to allocate code buffer";
  code_limit_ = code_buffer_ + buffer_size;
  // copy old code
  size_t code_size = emit_pos_ - old_buffer;
  memcpy(code_buffer_, old_buffer, code_size);
  emit_pos_ = code_buffer_ + code_size;
  // get rid of old buffer
  delete[] old_buffer;
  assert(emit_pos_ < code_limit_);
}


// Call this function to determine if code should be emitted.
// Be careful to only disable actual code emission and not
// any surrounding logic in order to preserve code generation
// invariants.
bool CodeGen::emit_ok() {
  if (FLAGS_eliminate_dead_code && dead_code_)
    return false;
  if (emit_pos_ >= code_limit_)
    MakeSpace();
  return true;
}


void CodeGen::emit_(Instr x) {
  if (emit_ok())
    *emit_pos_++ = x;
}


void CodeGen::emit_op(Opcode op) {
  AdjustStack(StackDelta(op));  // always do this
  if (emit_ok())
    emit_(op);
}


void CodeGen::emit_uint8(uint8 x) {
  if (emit_ok())
    Code::uint8_at(emit_pos_) = x;
}


void CodeGen::emit_int8(int8 x) {
  if (emit_ok())
    Code::int8_at(emit_pos_) = x;
}


void CodeGen::emit_int16(int16 x) {
  if (emit_ok())
    Code::int16_at(emit_pos_) = x;
}


void CodeGen::emit_int32(int32 x) {
  if (emit_ok())
    Code::int32_at(emit_pos_) = x;
}


void CodeGen::emit_pcoff(Code::pcoff x) {
  if (emit_ok())
    Code::pcoff_at(emit_pos_) = x;
}


void CodeGen::emit_ptr(const void* x) {
  if (emit_ok())
    Code::ptr_at(emit_pos_) = const_cast<void*>(x);
}


void CodeGen::emit_val(Val* x) {
  if (emit_ok())
    Code::val_at(emit_pos_) = x;
}

void CodeGen::EmitCounter(Node* x) {
  if (FLAGS_szl_bb_count) {
    int n = line_num_info_->length();
    Trace(&tlevel_, "count %d offset %d line %d", n, x->file_line()->offset(),
          x->file_line()->line());
    emit_op(count);
    emit_int32(n);
    line_num_info_->Append(x);
    assert(line_num_info_->length() == n+1);
  }
}


void CodeGen::align_emit_offset() {
  bool dead_code_saved_ = dead_code_;  // preserve state
  dead_code_ = false;  // avoid endless loop below
  // not particularly fast, but doesn't really matter
  while (emit_offset() % CodeDesc::kAlignment != 0)
    emit_op(nop);
  dead_code_ = dead_code_saved_;
}


void CodeGen::set_int32_at(int offset, int32 x) {
  assert(0 <= offset && offset + sizeof x <= emit_offset());
  Instr* pc = &(code_buffer()[offset]);
  Code::int32_at(pc) = x;
}


// ----------------------------------------------------------------------------
// Debugging

void CodeGen::Comment(const char* s) {
  if (debug_ || FLAGS_trace_code) {
    emit_op(comment);
    emit_ptr(s);
  }
}


// ----------------------------------------------------------------------------
// Control flow

BLabel* CodeGen::NewLabel(Proc* proc) {
  return NEWP(proc, BLabel);
}


void CodeGen::Bind(Label* L) {
  dead_code_ = false;  // code following a label target is alive
  down_cast<BLabel*>(L)->bind_to(emit_offset(), stack_height_, code_buffer());
}


void CodeGen::Branch(Opcode op, Label* L) {
  assert(op == branch || op == branch_true || op == branch_false ||
         op == createC || op == calli);
  if (UsesCC(op)) {
    // we must have cc set in this case
    if (! cc_set_)
      emit_op(set_cc);
  } else {
    // we shouldn't have cc set in this case
    assert(! cc_set_);
  }
  // don't call BLabel::offset() if we are in dead code
  // since it may cause this location to be patched later
  // and override other code instead
  if (emit_ok()) {
    emit_op(op);
    emit_pcoff(down_cast<BLabel*>(L)->offset(emit_offset(), stack_height_));
  }
  // at this point cc was set and consumed or not set
  // => we can safely clear it
  cc_set_ = false;
  // code following an unconditional branch is dead
  if (op == branch)
    dead_code_ = true;
}


// ----------------------------------------------------------------------------
// Expression code

void CodeGen::Visit(Node* x) {
  int beg = emit_offset();
  if (x->line_counter())
    EmitCounter(x);
  x->Visit(this);
  int end = emit_offset();
  x->set_code_range(beg, end);
}


void CodeGen::SetBP(int level) {
  int delta = bp_delta(level);
  assert(delta >= 0);
  // nothing to do if delta == 0 (same scope)
  if (delta > 0) {
    assert(delta == static_cast<uint8>(delta));  // delta must fit into uint8
    emit_op(set_bp);
    emit_uint8(delta);
  }
}


void CodeGen::Load(Expr* x, bool is_lhs) {
  BLabel ttarget(proc_);
  BLabel ftarget(proc_);
  int stack_height0 = stack_height_;  // the stack height before the load
  LoadConditional(x, is_lhs, &ttarget, &ftarget);
  if (cc_set_) {
    assert(x->type()->is_bool());
    emit_op(get_cc);
    cc_set_ = false;
  }
  if (ttarget.is_linked() || ftarget.is_linked()) {
    assert(x->type()->is_bool());
    // we have at least one conditional value
    // that has been "translated" into a branch,
    // thus it needs to be loaded explicitly again
    BLabel loaded(proc_);
    Branch(branch, &loaded);  // don't lose current TOS
    bool both = ttarget.is_linked() && ftarget.is_linked();
    // reincarnate "true", if necessary
    if (ttarget.is_linked()) {
      // at this point the value hasn't been loaded => adjust stack
      SetStack(stack_height0);
      Bind(&ttarget);
      PushBool(true);
    }
    // if both "true" and "false" need to be reincarnated,
    // jump across code for "false"
    if (both)
      Branch(branch, &loaded);
    // reincarnate "false", if necessary
    if (ftarget.is_linked()) {
      // at this point the value hasn't been loaded => adjust stack
      SetStack(stack_height0);
      Bind(&ftarget);
      PushBool(false);
    }
    // everything is loaded at this point
    Bind(&loaded);
  }
  assert(! cc_set_);
}


// Loads a value on TOS. If it is a boolean value, the result may have been
// (partially) translated into branches, or it may have set the condition code
// register. If the condition code register was set, cc_set_ is true.
void CodeGen::LoadConditional(Expr* x, bool is_lhs, Label* ttarget, Label* ftarget) {
  assert(! cc_set_);
  BCodeGenState* old_state = state_;
  BCodeGenState new_state(is_lhs, true, 0, down_cast<BLabel*>(ttarget), down_cast<BLabel*>(ftarget));
  state_ = &new_state;
  Visit(x);
  state_ = old_state;
}


void CodeGen::LoadComposite(Composite* x, int from, int n) {
  // push list elements on the stack in reverse order;
  // first element on top, last element at the bottom
  // (the language spec doesn't specify an evaluation order!)
  { StackMark sm(this);  // initX instructions consume all values
    for (int i = n; i-- > 0; )
      Load(x->at(from + i), false);
  }
}


// Load the expression as a left-hand side (LHS) value, but not to store into.
// Used to preserve side effects of LHS of dead assignments.
void CodeGen::LoadLHS(Expr* x) {
  assert(! cc_set_);
  BCodeGenState* old_state = state_;
  BCodeGenState new_state(true, true, 0, ttarget(), ftarget());
  state_ = &new_state;
  Visit(x);
  state_ = old_state;
}


void CodeGen::Store(Expr* x, int delta) {
  assert(! cc_set_);
  BCodeGenState* old_state = state_;
  BCodeGenState new_state(true, false, delta, ttarget(), ftarget());
  state_ = &new_state;
  Visit(x);
  state_ = old_state;
}


void CodeGen::StoreVarDecl(VarDecl* var) {
  assert(delta() == 0 || ! var->is_static());  // ++/-- only legal for locals
  Opcode op = VariableAccess(var->type(), false, is_lhs(), delta());
  SetBP(var->level());
  emit_op(op);
  emit_int16(var_index(var->offset()));
}


void CodeGen::Push(Val* val) {
  emit_op(pushV);
  emit_ptr(val);
}


void CodeGen::PushBool(bool b) {
  Push(Factory::NewBool(proc_, b));
}


void CodeGen::PushInt(szl_int i) {
  int8 i8 = i;
  if (i8 == i) {
    emit_op(push8);
    emit_int8(i8);
  } else {
    emit_op(pushV);
    emit_ptr(Factory::NewInt(proc_, i));
  }
}


void CodeGen::Dup(Type* type) {
  if (cc_set_) {
    assert(type->is_bool());
    emit_op(get_cc);
    cc_set_ = false;
  }
  emit_op(dupV);
}


void CodeGen::Pop(Type* type) {
  if (cc_set_) {
    assert(type->is_bool());
    cc_set_ = false;
  } else {
    emit_op(popV);
  }
}


void CodeGen::Compare(Type* type) {
  assert(! cc_set_);
  Opcode op = illegal;
  if (type->is_bool() || type->is_int() || type->is_fingerprint() || type->is_time())
    op = eql_bits;
  else if (type->is_float())
    op = eql_float;
  else if (type->is_string())
    op = eql_string;
  else if (type->is_bytes())
    op = eql_bytes;
  else
    ShouldNotReachHere();
  emit_op(op);
  assert(SetsCC(op));
  cc_set_ = true;
}


// Remove a result of the given type from the stack.
void CodeGen::DiscardResult(Type* type) {
  if (type->size() > 0)
    Pop(type);
}


// ----------------------------------------------------------------------------
// Statement code

void CodeGen::Execute(Statement* stat) {
  int stack_height0 = stack_height_;
  assert(! cc_set_);
  Visit(stat);
  assert(! cc_set_);
  assert(stack_height0 == stack_height_);  // stack height must not change
}


// ----------------------------------------------------------------------------
// Visitor functionality
// expressions

void CodeGen::DoBinary(Binary* x) {
  Trace t(&tlevel_, "(Binary");
  if (x->op() == Binary::LAND) {
    BLabel is_true(proc_);
    LoadConditional(x->left(), false, &is_true, ftarget());
    Branch(branch_false, ftarget());
    Bind(&is_true);
    LoadConditional(x->right(), false, ttarget(), ftarget());
  } else if (x->op() == Binary::LOR) {
    BLabel is_false(proc_);
    LoadConditional(x->left(), false, ttarget(), &is_false);
    Branch(branch_true, ttarget());
    Bind(&is_false);
    LoadConditional(x->right(), false, ttarget(), ftarget());
  } else {
    Load(x->left(), false);
    Load(x->right(), false);
    emit_op(x->opcode());
    assert(! cc_set_);
    cc_set_ = SetsCC(x->opcode());
  }
}


 void CodeGen::LenIntrinsic(Expr* var) {
  // The caller emits the operand push.
  // Ugly: build an argument list from the array
  List<Expr*> *args = List<Expr*>::New(proc_);
  args->Append(var);
  Intrinsic* fun = SymbolTable::universe()->LookupOrDie("len")->AsIntrinsic();
  Intrinsic::CFunction target = Intrinsics::TargetFor(proc_, fun, args);
  emit_op(fun->can_fail() ? callc : callcnf);
  emit_ptr((const void*)target);
}


void CodeGen::DoCall(Call* x) {
  Trace t(&tlevel_, "(Call");
  const List<Expr*>* args = x->args();
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
        if (args->length() > 1  && args->at(1)->type()->is_string()) {
          // push args in reverse order, leaving format string at TOS
          { StackMark sm(this);  // fd_print consumes the arguments
            for (int i = args->length(); --i >= 1; )
              Load(args->at(i), false);
            // push file descriptor
            PushInt(1);
          }
          emit_op(fd_print);
          return;
        }
      }
      if (cmd == "ref") {
        // for debugging: print ref count of a value
        Load(args->at(1), false);
        emit_op(debug_ref);
        return;
      }
      Unimplemented();
      return;
    }

    if (fun->kind() == Intrinsic::DEF) {
      { TrapHandler handler(this, ftarget(), NULL, true, args->at(0));
        // evaluate expr & throw away result
        Load(args->at(0), false);
        Pop(args->at(0)->type());
      }
      // if we reach this point, the expression was defined
      PushBool(true);
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
      Opcode op = is_inproto ? ftestB : fclearB;
      // emit code
      Load(s->var(), make_unique);
      emit_op(op);
      emit_int32(i);
      return;
    }

    if (fun->kind() == Intrinsic::UNDEFINE) {
      Variable* v = args->at(0)->AsVariable();
      assert(v != NULL);
      SetBP(v->level());
      emit_op(undefine);
      emit_int16(var_index(v->offset()));
      return;
    }

    if (fun->kind() == Intrinsic::SORT ||
        fun->kind() == Intrinsic::SORTX) {
      // TODO: we could put this in TargetFor and have two
      // non-variadic C functions.  Or even four non-variadic szl
      // ones.
      assert(fun->function() != NULL);
      { StackMark sm(this);
        // supply placeholder for missing second argument
        if (args->length() == 1)
          Push(NULL);
        else
          Load(args->at(1), false);
        Load(args->at(0), false);
      }
      emit_op(fun->can_fail() ? callc : callcnf);
      emit_ptr((const void*)fun->function());
      AdjustStack(x->type()->is_void() ? 0 : 1);  // note: duplicated below
      return;
    }

    // all normal calls can be handled trivially.
    // push args (first argument last)
    { StackMark sm(this);  // calls consume all arguments
      for (int i = args->length(); i-- > 0; )
        Load(args->at(i), false);
    }

    // special cases - match*() has precompiled pattern (or NULL)
    if (fun->kind() == Intrinsic::MATCH) {
      emit_op(match);
      emit_ptr(CompiledRegexp(args->at(0), proc_, &error_count_));

    } else if (fun->kind() == Intrinsic::MATCHPOSNS) {
      emit_op(matchposns);
      emit_ptr(CompiledRegexp(args->at(0), proc_, &error_count_));

    } else if (fun->kind() == Intrinsic::MATCHSTRS) {
      emit_op(matchstrs);
      emit_ptr(CompiledRegexp(args->at(0), proc_, &error_count_));

    } else {
      // get the target, mapping overloaded intrinsics as needed
      Intrinsic::CFunction target = Intrinsics::TargetFor(proc_, fun, args);
      emit_op(fun->can_fail() ? callc : callcnf);
      emit_ptr((const void*)target);
    }

  } else {
    // push args (first argument last)
    { StackMark sm(this);  // calls consume all arguments
      for (int i = args->length(); i-- > 0; )
        Load(args->at(i), false);
    }
    if (x->fun()->AsFunction() != NULL) {
      Function* fun = x->fun()->AsFunction();
      SetBP(fun->context_level());
      if (fun->entry() == NULL)
        fun->set_entry(CodeGen::NewLabel(proc_));
      // stack height at function entry points is 0
      // => temporarily reset it so the Branch assertions hold
      StackMark sm(this);  // use to reset stack height
      SetStack(0);
      Branch(calli, fun->entry());
    } else {
      Load(x->fun(), false);  // load closure
      emit_op(call);  // issue the call
    }
  }

  // when an undefined value is returned the function goes to the trap handler
  // *after* restoring the stack frame so we see this offset
  if (x->CanCauseTrap(false)) {
    assert(current_trap_range_ != NULL);
    current_trap_range_->AddTrap(emit_offset() - 1, NULL);
  }

  // adjust stack height according to result
  AdjustStack(x->type()->is_void() ? 0 : 1);  // TODO: cleanup!
  //AdjustStack(Align(x->type()->size(), sizeof(Val*)) / sizeof(Val*));

  // note: unused results, if any, will be discarded by DoExprStat
}


void CodeGen::DoComposite(Composite* x) {
  Trace t(&tlevel_, "(Composite");
  // used to construct array, bytes, map, string and tuple literals.
  assert(!x->type()->is_incomplete());
  const int n = x->length();
  // issue creation instructions
  Type* type = x->type();
  if (type->is_array()) {
    emit_op(createA);
    emit_int32(n);
    emit_ptr(type);
    for (int from = 0; from < n; from += kNumElems) {
      const int elems = std::min(n - from, kNumElems);
      LoadComposite(x, from, elems);
      emit_op(initA);
      emit_int32(from);
      emit_int32(elems);
    }
  } else if (type->is_bytes()) {
    LoadComposite(x, 0, n);
    emit_op(createB);
    emit_int32(n);
  } else if (type->is_map()) {
    assert(n % 2 == 0);
    emit_op(createM);
    emit_int32(n / 2);
    emit_ptr(type);
    for (int from = 0; from < n; from += kNumElems) {
      const int elems = std::min(n - from, kNumElems);
      assert(elems % 2 == 0);
      LoadComposite(x, from, elems);
      emit_op(initM);
      emit_int32(elems);
    }
  } else if (type->is_string()) {
    LoadComposite(x, 0, n);
    emit_op(createStr);
    emit_int32(n);
  } else if (type->is_tuple()) {
    emit_op(createT);
    emit_ptr(type);
    for (int from = 0; from < n; from += kNumElems) {
      const int elems = std::min(n - from, kNumElems);
      LoadComposite(x, from, elems);
      emit_op(initT);
      emit_int32(from);
      emit_int32(elems);
    }
  } else {
    ShouldNotReachHere();
  }
}


void CodeGen::DoConversion(Conversion* x) {
  Trace t(&tlevel_, "(Conversion op = %s", ConversionOp2String(x->op()));
  { StackMark sm(this);  // conversions consume all arguments and src
    // load extra arguments
    for (int i = x->params()->length(); i-- > 0; )
      Load(x->params()->at(i), false);
    // load src
    Load(x->src(), false);
  }
  // emit appropriate instruction
  if (x->kind() == Conversion::kBasicConv) {
    emit_op(basicconv);
    emit_(x->op());

  } else if (x->kind() == Conversion::kArrayToArrayConv) {
    emit_op(arrayconv);
    assert(ImplementedArrayToArrayConversion(x->op()));
    emit_(x->op());

  } else {
    assert(x->kind() == Conversion::kArrayToMapConv);
    assert(x->params()->is_empty());  // too hard otherwise
    emit_op(mapconv);
    emit_ptr(x->type());
    assert(ImplementedArrayToMapConversion(x->op()));
    assert(ImplementedArrayToMapConversion(x->key_op()));
    emit_(x->key_op());
    emit_(x->op());
  }

  // Emit result type when it is not determined by the op.
  // Not needed by ArrayToMap - we have already emitted the map type.
  if (x->kind() != Conversion::kArrayToMapConv &&
      (x->op() == typecast || x->op() == bytes2proto || x->op() == tuple2tuple))
    emit_ptr(x->type());

  // Emit source type for proto2bytes.
  // (Would be needed for ArrayToMap if we supported it.)
  if (x->op() == proto2bytes)
      emit_ptr(x->src()->type());
 
  // adjust stack according to result
  AdjustStack(Align(x->type()->size(), sizeof(Val*)) / sizeof(Val*));
}


void CodeGen::DoDollar(Dollar* x) {
  Trace t(&tlevel_, "(Dollar");
  if (x->AsComposite() != NULL) {
    emit_op(pushV);
    emit_val(TaggedInts::MakeVal(x->AsComposite()->length()));
  } else if (x->length_temp() != NULL) {
    Visit(x->length_temp());
  } else {
    Load(x->array(), false);
    LenIntrinsic(x->array());
    // stack size doesn't change
  }
}


void CodeGen::DoFunction(Function* x) {
  Trace t(&tlevel_, "(Function");
  // stack height at function entry points is 0
  // => temporarily reset it so the Branch assertions hold
  { StackMark sm(this);  // use to reset stack height
    SetStack(0);
    int delta = bp_delta(x->context_level());
    assert(0 <= delta && delta < 256);
    Branch(createC, x->entry());
    emit_uint8(delta);
    // TODO: For now we emit also the function type so
    // we can create the corresponding ClosureVal - this
    // should be sufficient and we don't really need the
    // branch offset - simplify this code eventually!
    emit_ptr(x->type());
  }
  // the stack has really grown by 1 (the closure)
  // => correct it
  AdjustStack(1);
}


void CodeGen::DoSelector(Selector* x) {
  Trace t(&tlevel_, "(Selector");
  TupleType* tuple = x->var()->type()->as_tuple();
  assert(tuple != NULL);
  Load(x->var(), is_lhs());
  // set the inproto bit if necessary
  if (is_lhs()) {
    emit_op(fsetB);  // leaves the value on the stack
    emit_int32(tuple->inproto_index(x->field()));
  }
  Opcode op = SelectorAccess(x->field()->type(), is_load(), is_lhs(), delta());
  emit_op(op);
  emit_int16(x->field()->slot_index());
  if (delta() != 0) {
    assert(op == finc64);
    emit_int8(delta());
  }
}


void CodeGen::DoRuntimeGuard(RuntimeGuard* x) {
  Trace t(&tlevel_, "(RuntimeGuard");
  // evaluate the guard condition and trap if false
  BLabel tguard(proc_);
  BLabel fguard(proc_);
  LoadConditional(x->guard(), false, &tguard, &fguard);
  if (! cc_set_)
    emit_op(set_cc);  // set cc if necessary
  // a false guard was translated into a branch
  // => materialize the false value (this could
  // be done better, but this is a rare case)
  if (fguard.is_linked()) {
    BLabel L(proc_);
    Branch(branch, &L);
    Bind(&fguard);
    PushBool(false);
    emit_op(set_cc);
    Bind(&L);
  }
  // trap if the guard condition is not true
  emit_op(trap_false);
  emit_ptr(x->msg());
  cc_set_ = false;
  // evaluate the expression
  Bind(&tguard);
  Visit(x->expr());
}


void CodeGen::DoIndex(Index* x) {
  Trace t(&tlevel_, "(Index");
  Type* type = x->var()->type();
  if (type->is_indexable()) {
    Load(x->var(), is_lhs());
    if (x->length_temp() != NULL) {
      // var is nontrivial and index uses "$"; save the length in a temp
      emit_op(dupV);
      LenIntrinsic(x->var());
      Store(x->length_temp(), 0);
    }
    Load(x->index(), false);  // must follow setting length temp: might use it
    Opcode op = IndexedAccess(type, is_load(), is_lhs(), delta());
    emit_op(op);
    if (delta() != 0)
      emit_int8(delta());
  } else if (type->is_map()) {
    Load(x->index(), false);
    Load(x->var(), is_lhs());
    Opcode opkey = MappedKey(type->as_map(), is_load(), is_lhs(), delta(), proc_, &error_count_);
    Opcode opvalue = MappedValue(type->as_map(), is_load(), is_lhs(), delta(), proc_, &error_count_);
    // avoid Unimplemented() - keep regression test failure clean.
    if (opkey == illegal || opvalue == illegal) {
      emit_op(stop);
      emit_ptr(proc_->PrintString("can't codegen index %T", type));
    } else {
      emit_op(opkey);
      emit_op(opvalue);
    }
    if (delta() != 0)
      emit_int8(delta());
  } else {
    // there are no other indexable types
    ShouldNotReachHere();
  }
}


void CodeGen::DoNew(New* x) {
  Trace t(&tlevel_, "(New");
  Type* type = x->type();
  assert(type->is_allocatable());
  if (type->is_array()) {
    Load(x->init(), false);
    Load(x->length(), false);
    emit_op(newA);
    emit_ptr(type);
  } else if (type->is_bytes()) {
    Load(x->init(), false);
    Load(x->length(), false);
    emit_op(newB);
  } else if (type->is_map()) {
    Load(x->length(), false);
    emit_op(newM);
    emit_ptr(type);
  } else if (type->is_string()) {
    Load(x->init(), false);
    Load(x->length(), false);
    emit_op(newStr);
  } else {
    // there are no other allocatable types
    ShouldNotReachHere();
  }
}


void CodeGen::DoRegex(Regex* x) {
  Trace t(&tlevel_, "(Regex");
  const char* pat = RegexPattern(x, proc_, &error_count_);
  if (strcmp(pat, "") == 0)
    return;  // do not continue with potentially bad data

  DoLiteral(Literal::NewString(proc_, x->file_line(), NULL, pat));
}


void CodeGen::DoSaw(Saw* x) {
  Trace t(&tlevel_, "(Saw");
  Comment(proc_->PrintString("%n", source(), x));
  // set up result array (initially empty)
  emit_op(createA);
  emit_int32(0);
  emit_ptr(SymbolTable::array_of_string_type());
  // load string to be sawn apart
  Load(x->str(), false);

  int argn;
  for (int arg0 = 0; arg0 < x->args()->length(); arg0 = argn) {
    StackMark sm(this);
    // load regexes & skip info
    for (argn = arg0; argn < x->args()->length() && x->flags()->at(argn) != Saw::REST; argn++)
        ;
    // argn >= x->args()->length() || x->flags()[argn] == Saw::REST
    // args()->at(argn - 1) is the last regex we are using in this iteration
    const int regex_count = argn - arg0;

    // invoke saw if necessary
    if (regex_count > 0) {
      // we have at least one regex
      // load regex arguments
      for (int i = argn - 1; i >= arg0; i--)
          Load(x->args()->at(i), false);
      // load flag arguments
      for (int i = argn - 1; i >= arg0; i--) {
        assert(x->flags()->at(i) != Saw::REST);
        PushInt(x->flags()->at(i));
      }
      // load count
      Load(x->count(), false);
      // invoke the saw
      emit_op(saw);
      emit_uint8(regex_count);
      emit_ptr((void*)x->static_args());  // space for a regex cache if desired
    }

    // assign rest if present
    if (argn < x->args()->length()) {
      // we must have a rest => assign it
      emit_op(dupV);
      Store(x->args()->at(argn), 0);
      // skip the rest argument
      argn++;
    }
  }

  // get rid of str
  emit_op(popV);

  // now we have the resulting array of string on the stack
  assert(x->type()->IsEqual(SymbolTable::array_of_string_type(), false));
}


void CodeGen::DoSlice(Slice* x) {
  Trace tr(&tlevel_, "(Slice");
  Load(x->var(), is_lhs());
  if (x->length_temp() != NULL) {
    // var is nontrivial and beg/end use "$"; save the length in a temp
    emit_op(dupV);
    LenIntrinsic(x->var());
    Store(x->length_temp(), 0);
  }
  Load(x->beg(), false);
  Load(x->end(), false);
  if (is_lhs() && is_load()) {
    Error("can't handle sliced store of arrays yet");
    return;  // do not continue with potentially bad data
  }
  Type* t = x->type();
  Opcode lop = illegal;
  Opcode sop = sstoreV;
  if (t->is_array()) {
    lop = sloadV;
  } else if (t->is_bytes()) {
    lop = sload8;
  } else if (t->is_string()) {
    lop = sloadR;
  } else {
    ShouldNotReachHere();
  }
  emit_op(is_load() ? lop : sop);
}


void CodeGen::DoStatExpr(StatExpr* x) {
  Trace tr(&tlevel_, "(StatExpr");
  x->set_exit(CodeGen::NewLabel(proc_));  // target for result statement
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
  emit_op(stop);
  // Generate a run-time error if no result statement is executed.
  FileLine* fl = x->file_line();
  szl_string msg;
  msg = proc_->PrintString("missing result in ?{} that begins at %L", fl);
  emit_ptr(msg);
  Bind(x->exit());
  Load(x->var(), false);
}


void CodeGen::DoLiteral(Literal* x) {
  Trace(&tlevel_, "Literal %n", source(), x);
  emit_op(pushV);
  emit_val(x->val());
}


void CodeGen::DoVariable(Variable* x) {
  Trace(&tlevel_, "Variable %n", source(), x);
  assert(delta() == 0 || ! x->is_static());  // ++//-- only legal for locals
  Opcode op = VariableAccess(x->type(), is_load(), is_lhs(), delta());
  SetBP(x->level());
  emit_op(op);
  emit_int16(var_index(x->offset()));
  if (delta() != 0) {
    assert(op == inc64);
    emit_int8(delta());
  }
  if (is_load() && x->CanTrap()) {
    // remember this trap site and the variable that was loaded
    assert(current_trap_range_ != NULL);
    x->var_decl()->UsesTrapinfoIndex(proc_);  // make sure we have a slot for it
    current_trap_range_->AddTrap(emit_offset() - 1, x->var_decl());
  }
}


void CodeGen::DoTempVariable(TempVariable* x) {
  if (x->init() != NULL && !x->initialized()) {
    Load(x->init(), is_lhs());
    emit_op(dupV);
    StoreVar(x);
    x->set_initialized();
  } else {
    DoVariable(x);
  }
}


// ----------------------------------------------------------------------------
// Visitor functionality
// statements

void CodeGen::DoAssignment(Assignment* x) {
  Trace t(&tlevel_, "(Assignment");
  BLabel exit(proc_);
  { TrapHandler handler(this, &exit, UndefVar(x->lvalue())->var_decl(), false, x);
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


void CodeGen::DoBlock(Block* x) {
  Trace t(&tlevel_, "(Block");
  for (int i = 0; i < x->length(); i++)
    Execute(x->at(i));
}


void CodeGen::DoBreak(Break* x) {
  Trace t(&tlevel_, "(Break");
  Branch(branch, x->stat()->exit());
}


void CodeGen::DoContinue(Continue* x) {
  Trace t(&tlevel_, "(Continue");
  Branch(branch, x->loop()->cont());
}


void CodeGen::DoTypeDecl(TypeDecl* x) {
  // nothing to do
}


void CodeGen::DoVarDecl(VarDecl* x) {
  Trace t(&tlevel_, "(VarDecl %s", x->name());
  // either do all statics or all locals
  if (x->is_static() == do_statics()) {
    // determine initial variable value
    if (x->type()->is_output()) {
      Comment(proc_->PrintString("initialize %s", x->name()));
      assert(do_statics());  // output variables are static
      // initialize output variable
      { TableInfo* t = TableInfo::New(proc_, x->name(), x->type()->as_output());
        tables_->Append(t);
        // we allow arbitrary expressions as table parameters, so
        // we have to catch and die in cases when param evaluation
        // results in undefined values or values out of range
        BLabel exit(proc_);
        { TrapHandler handler(this, &exit, NULL, false, x);
          Expr* param = x->type()->as_output()->param();
          if (param != NULL) {
            Load(param, false);
          } else {
            PushInt(-1); // dummy
          }
          SetBP(x->level());
          emit_op(openO);
          emit_int16(var_index(x->offset()));
          emit_int16(tables_->length() - 1);  // tables_ index for t
        }
        Bind(&exit);
      }
    } else if (x->init() != NULL) {
      Comment(proc_->PrintString("initialize %s", x->name()));
      BLabel exit(proc_);
      // static variables don't have a defined bit - don't provide
      // the variable information; also traps are never silent here -
      // non-static variables are silently initialized always (i.e.,
      // if the rvalue is undefined the lvalue is undefined even
      // if --ignore_undefs is not set)
      { TrapHandler handler(this, &exit, (do_statics() ? NULL : x), ! do_statics(), x->init());
        Load(x->init(), false);
        StoreVarDecl(x);
      }
      Bind(&exit);
    } else {
      // nothing to do (all variables are NULLed out in the beginning)
    }
  }
}


void CodeGen::DoEmit(Emit* x) {
  Trace t(&tlevel_, "(Emit");
  Comment(proc_->PrintString("emit %n", source(), x));
  List<VarDecl*>* index_decls = x->index_decls();
  List<Expr*>* indices = x->indices();
  int num_index_decls = index_decls->length();
  BLabel exit(proc_);
  { TrapHandler handler(this, &exit, NULL, false, x);
    StackMark sm(this);
    // push 'weight' on stack, if any
    if (x->weight() != NULL)
      Load(x->weight(), false);

    if (x->elem_format() != NULL) {
      // we have an element format
      // first assign value to element variable
      // do not use Visit() because we want undef checking enabled
      Load(x->value(), false);
      StoreVarDecl(x->elem_decl());
      // call format() and push result
      Load(x->elem_format(), false);
    } else {
      // push 'value' on stack
      // push value on stack
      Load(x->value(), false);
    }

    if (x->index_format() != NULL) {
      // we have an index format
      // first assign indices to index variables
      // do not use Visit() because we want undef checking enabled
      for (int i = 0; i < num_index_decls; i++) {
        Load(indices->at(i), false);
        StoreVarDecl(index_decls->at(i));
      }
      // call format() and push result
      Load(x->index_format(), false);
    } else {
      // push indices on stack, if any
      for (int i = num_index_decls; --i >= 0; )
        Load(indices->at(i), false);
    }
 
    // push the output variable
    Load(x->output(), false);
  }

  // generate the instruction
  // tos: ... 'value' 'indices' 'var_index' -> ...
  // with: 'value'   = actual value or formatted value string
  //       'indices' = actual indices or formatted index string
  //       'var_index' = global index of the output variable
  SetBP(0);  // table is a global variable
  emit_op(emit);

  Bind(&exit);
}


void CodeGen::DoEmpty(Empty* x) {
  // nothing to do
}


void CodeGen::DoExprStat(ExprStat* x) {
  Trace t(&tlevel_, "(ExprStat");
  BLabel exit(proc_);
  { TrapHandler handler(this, &exit, NULL, false, x->expr());
    Expr* e = x->expr();
    Load(e, false);
    DiscardResult(e->type());
  }
  Bind(&exit);
}


void CodeGen::DoIf(If* x) {
  Trace t(&tlevel_, "(If");
  // generate different code depending on which
  // parts of the if statement are present or not
  bool has_then = x->then_part()->AsEmpty() == NULL;
  bool has_else = x->else_part()->AsEmpty() == NULL;

  BLabel exit(proc_);
  if (has_then && has_else) {
    BLabel then(proc_);
    BLabel else_(proc_);
    // if (cond)
    { TrapHandler handler(this, &exit, NULL, false, x->cond());
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
    BLabel then(proc_);
    // if (cond)
    { TrapHandler handler(this, &exit, NULL, false, x->cond());
      LoadConditional(x->cond(), false, &then, &exit);
      Branch(branch_false, &exit);
    }
    // then
    Bind(&then);
    Execute(x->then_part());

  } else if (has_else) {
    assert(!has_then);
    BLabel else_(proc_);
    // if (!cond)
    { TrapHandler handler(this, &exit, NULL, false, x->cond());
      LoadConditional(x->cond(), false, &exit, &else_);
      Branch(branch_true, &exit);
    }
    // else
    Bind(&else_);
    Execute(x->else_part());

  } else {
    assert(!has_then && !has_else);
    // if (cond)
    { TrapHandler handler(this, &exit, NULL, false, x->cond());
      LoadConditional(x->cond(), false, &exit, &exit);
      Pop(SymbolTable::bool_type());
    }
  }

  // end
  Bind(&exit);
}


void CodeGen::DoIncrement(Increment* x) {
  Trace t(&tlevel_, "(Increment");
  BLabel exit(proc_);
  assert(x->delta() == 1 || x->delta() == -1);
  { TrapHandler handler(this, &exit, UndefVar(x->lvalue())->var_decl(),
                        false, x->lvalue());
    Store(x->lvalue(), x->delta());
  }
  Bind(&exit);
}


void CodeGen::DoResult(Result* x) {
  Trace t(&tlevel_, "(Result");
  Comment(proc_->PrintString("result %n", source(), x->expr()));
  BLabel exit(proc_);
  Variable* tempvar = x->statexpr()->var();
  { TrapHandler handler(this, &exit, UndefVar(tempvar)->var_decl(), false,
                        x->expr());
    Load(x->expr(), false);
    Store(tempvar, 0);
  }
  Bind(&exit);
  Branch(branch, x->statexpr()->exit());
}


void CodeGen::DoReturn(Return* x) {
  Trace t(&tlevel_, "(Return");
  if (x->has_result()) {
    Comment(proc_->PrintString("return %n", source(), x->result()));
    TrapHandler handler(this, global_trap_handler_, NULL, true, x->result());
    Load(x->result(), false);
    emit_op(retV);  // must be inside TrapHandler scope (stack_height_ asserts!)
    // remember this trap site and that it was a return
    if (x->result()->CanTrap()) {
      assert(current_trap_range_ != NULL);
      current_trap_range_->AddTrap(emit_offset() - 1, NULL);
    }
  } else {
    Comment("return");
    emit_op(ret);
  }
  emit_int16(function_->frame_size() / sizeof(Val*));
  // code following a return is dead
  dead_code_= true;
}


void CodeGen::DoSwitch(Switch* x) {
  Trace t(&tlevel_, "(Switch");
  BLabel done(proc_);
  x->set_exit(CodeGen::NewLabel(proc_));
  Comment(proc_->PrintString("switch (%n)", source(), x->tag()));
  // switch (tag)
  TrapHandler handler(this, &done, NULL, false, x->tag());
  Load(x->tag(), false);
  // handle each case
  Type* tag_type = x->tag()->type();
  List<Case*>* cases = x->cases();
  for (int i = 0; i < cases->length(); i++) {
    BLabel next_case(proc_);
    BLabel case_stat(proc_);
    Case* case_ = cases->at(i);
    // handle each label
    List<Expr*>* labels = case_->labels();
    for (int i = 0; i < labels->length(); i++) {
      Expr* x = labels->at(i);
      if (i + 1 < labels->length()) {
        // not the final label in a case list
        BLabel next_label(proc_);
        { TrapHandler handler(this, &next_label, NULL, false, x);
          Dup(tag_type);
          Load(x, false);
          Compare(tag_type);
          Branch(branch_true, &case_stat);
        }
        Bind(&next_label);
      } else {
        // final label in a case list
        TrapHandler handler(this, &next_case, NULL, false, x);
        Dup(tag_type);
        Load(x, false);
        Compare(tag_type);
        Branch(branch_false, &next_case);
      }
    }
    Bind(&case_stat);
    { StackMark sm(this);
      Pop(tag_type);  // discard tag
      TrapHandler handler(this, x->exit(), NULL, false, labels->at(0));
      Execute(case_->stat());
      Branch(branch, x->exit());
    }
    Bind(&next_case);
  }
  // handle default
  Pop(tag_type);  // discard tag
  Execute(x->default_case());
  // end
  Bind(x->exit());
  Bind(&done);
}


void CodeGen::DoWhen(When* x) {
  Trace t(&tlevel_, "(When");
  if (FLAGS_v > 0)
    F.print("rewrite of when:\n%1N\n", x->rewritten());
  Visit(x->rewritten());
}


void CodeGen::DoLoop(Loop* x) {
  Trace t(&tlevel_, "(Loop");
  BLabel entry(proc_);
  BLabel loop(proc_);
  x->set_cont(CodeGen::NewLabel(proc_));  // for continue statement
  x->set_exit(CodeGen::NewLabel(proc_));  // for break statement
  Symbol sym = static_cast<Symbol>(x->sym());
  Comment(proc_->PrintString("%s loop (%n)", Symbol2String(sym),
                             source(), x->cond()));
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
    TrapHandler handler(this, x->exit(), NULL, false, x->cond());
    LoadConditional(x->cond(), false, &loop, x->exit());
    Branch(branch_true, &loop);
  }
  // end
  Bind(x->exit());
}


}  // namespace sawzall
